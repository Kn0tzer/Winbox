// FD-55 packaging step: build the pre-warmed Godot demo archive.
//
// The Godot demo's first run spends far longer than the demo window paying
// one-time startup costs (guest shader/pipeline compilation at interpreter
// speed), so the standard demo check sees zero presented frames. Those
// caches live in the guest home tree — the same tree the launcher's
// persistence layer restores before boot — so a capture that ran the demo
// and banked cache state can seed that state into the archive itself, and
// every later fresh run starts warm.
//
// This script is the repeatable step, in two halves:
//   1. capture: web/tests/godot-warm-capture.mjs runs the stock demo and
//      harvests the persistence store (the files a returning session would
//      have restored). Captures compound: capture N+1 runs against the
//      archive packaged from captures 1..N and banks the next slice of
//      state; pass several --capture dirs and their harvests merge.
//   2. pack (this script): folds the curated subset of the merged harvest
//      into a NEW archive next to the pristine one. The pristine archive
//      is never modified. The curation lives in the committed list file
//      (godot-squash-warm-files.txt): seed lines name cache files (with an
//      optional `archive-path <= harvest-path` rename), and `!` lines name
//      reviewed exclusions (state that is deliberately NOT seeded, with
//      the reason). The packager FAILS LOUDLY if a capture produced a
//      home-tree file that is neither seeded, excluded, nor part of the
//      pristine package — new guest state must be reviewed, never silently
//      baked or silently dropped.
//
// The DEMO CHECK (the unmodified demo-matrix) is the only proof of the end
// state; this script claims no demo outcome.
//
// Usage:
//   node tools/demo-packaging/build-warmed-archive.mjs \
//     --source /home/ubuntu/kn0tzer/games/godot-squash.zip \
//     --capture test-results/fd55prewarm/warm-<stamp> [--capture <dir>...] \
//     --out /home/ubuntu/kn0tzer/games/godot-squash-warmed.zip
import { createHash } from 'node:crypto';
import { createReadStream, createWriteStream } from 'node:fs';
import { readFile, stat, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, '../..');
const args = process.argv.slice(2);
const arg = (name, dflt) => {
  const at = args.indexOf('--' + name);
  return at >= 0 ? args[at + 1] : dflt;
};
const allArgs = name => {
  const out = [];
  for (let i = 0; i < args.length; i++) if (args[i] === '--' + name) out.push(args[i + 1]);
  return out;
};
const source = resolve(arg('source', '/home/ubuntu/kn0tzer/games/godot-squash.zip'));
const out = resolve(arg('out', '/home/ubuntu/kn0tzer/games/godot-squash-warmed.zip'));
const listPath = resolve(arg('list', join(here, 'godot-squash-warm-files.txt')));
const captureDirs = allArgs('capture').map(d => resolve(repo, d));
if (captureDirs.length === 0) throw new Error('at least one --capture <dir> is required');

// --- merged harvest ---------------------------------------------------------
const harvested = new Map();   // harvest path -> {path, bytes, sha256, file}
let lastSummary = null;
for (const dir of captureDirs) {
  const manifest = JSON.parse(await readFile(join(dir, 'harvest-manifest.json'), 'utf8'));
  for (const f of manifest) harvested.set(f.path, { ...f, file: join(dir, 'harvest', f.path) });
  lastSummary = JSON.parse(await readFile(join(dir, 'summary.json'), 'utf8'));
}
// A capture set is packageable when it banked new guest state after the
// engine actually started. The packaging loop exists because one capture
// may not finish the whole first-run workload; each packaged seed still
// has to be real banked state, never an empty run in a new envelope.
if (harvested.size === 0 || !lastSummary?.events?.godotBanner) {
  throw new Error('refusing to package: the captures banked no new guest state after engine start');
}

// --- curation list ------------------------------------------------------------
// Seed line:    [archive-path <=] harvest-path   # reason
// Exclusion:    ! harvest-path                   # reason
const seeds = new Map();       // harvest path -> archive path
const exclusions = new Set();
for (const line of (await readFile(listPath, 'utf8')).split(/\r?\n/)) {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith('#')) continue;
  const [body] = trimmed.split('#');
  const text = body.trim();
  if (!text) continue;
  if (text.startsWith('!')) { exclusions.add(text.slice(1).trim()); continue; }
  const parts = text.split('<=').map(s => s.trim());
  if (parts.length === 2) seeds.set(parts[1], parts[0]);
  else seeds.set(parts[0], parts[0]);
}
if (seeds.size === 0) throw new Error(`curation list ${listPath} names no seed files`);
const PRISTINE = new Set(['home/username/apps/SquashTheCreeps.exe', 'home/username/apps/SquashTheCreeps.pck']);
for (const path of seeds.keys()) {
  if (!harvested.has(path)) throw new Error(`seed file missing from the merged capture harvest: ${path}`);
}
for (const path of exclusions) {
  if (!harvested.has(path)) throw new Error(`excluded file not present in the merged capture harvest (stale exclusion?): ${path}`);
}
for (const path of harvested.keys()) {
  if (!seeds.has(path) && !exclusions.has(path) && !PRISTINE.has(path)) {
    throw new Error(`capture produced unreviewed home-tree state, refusing to package: ${path} — review it and add it to ${listPath} as a seed or a '!' exclusion with a reason, then re-run`);
  }
}

// --- minimal stored-zip reader for the source archive ----------------------
async function readCentralDirectory(path) {
  const fh = await readFile(path);
  let eocd = -1;
  for (let i = fh.length - 22; i >= Math.max(0, fh.length - 22 - 65536); i--) {
    if (fh.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error('EOCD not found in ' + path);
  const count = fh.readUInt16LE(eocd + 10);
  let off = fh.readUInt32LE(eocd + 16);
  const entries = [];
  for (let n = 0; n < count; n++) {
    if (fh.readUInt32LE(off) !== 0x02014b50) throw new Error('bad central record at ' + off);
    const method = fh.readUInt16LE(off + 10);
    const csize = fh.readUInt32LE(off + 20);
    const usize = fh.readUInt32LE(off + 24);
    const nameLen = fh.readUInt16LE(off + 28);
    const extraLen = fh.readUInt16LE(off + 30);
    const commentLen = fh.readUInt16LE(off + 32);
    const localOff = fh.readUInt32LE(off + 42);
    const name = fh.subarray(off + 46, off + 46 + nameLen).toString('utf8');
    const lnameLen = fh.readUInt16LE(localOff + 26);
    const lextraLen = fh.readUInt16LE(localOff + 28);
    entries.push({ name, method, csize, usize, dataOff: localOff + 30 + lnameLen + lextraLen });
    off += 46 + nameLen + extraLen + commentLen;
  }
  return entries;
}

// --- CRC32 ------------------------------------------------------------------
const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1); t[n] = c >>> 0; }
  return t;
})();
function crc32Step(crc, chunk) {
  for (let i = 0; i < chunk.length; i++) crc = crcTable[(crc ^ chunk[i]) & 0xFF] ^ (crc >>> 8);
  return crc;
}
async function crc32OfFile(path) {
  let crc = 0xFFFFFFFF;
  for await (const chunk of createReadStream(path)) crc = crc32Step(crc, chunk);
  return (crc ^ 0xFFFFFFFF) >>> 0;
}
const crc32OfBuffer = buf => (crc32Step(0xFFFFFFFF, buf) ^ 0xFFFFFFFF) >>> 0;

// DOS date/time pinned to the pristine archive's convention (2026-01-01).
const DOS_TIME = 0, DOS_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1;
const EXT_ATTR = 0x1a40000; // 0644 << 16, as in the pristine archive

async function main() {
  const srcEntries = await readCentralDirectory(source);
  const srcFh = await readFile(source); // 111 MB: buffered copy keeps offsets trivially correct

  const ws = createWriteStream(out);
  const central = [];
  let offset = 0;
  const write = async buf => { ws.write(buf); offset += buf.length; };

  async function addStored(name, data) {
    let crc, size;
    if (data.fromFile) { crc = await crc32OfFile(data.fromFile); size = (await stat(data.fromFile)).size; }
    else { crc = crc32OfBuffer(data.fromBuffer); size = data.fromBuffer.length; }
    const nameBuf = Buffer.from(name, 'utf8');
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0x0800, 6);              // UTF-8 filename flag
    local.writeUInt16LE(0, 8);                   // method: stored
    local.writeUInt16LE(DOS_TIME, 10); local.writeUInt16LE(DOS_DATE, 12);
    local.writeUInt32LE(crc, 14); local.writeUInt32LE(size, 18); local.writeUInt32LE(size, 22);
    local.writeUInt16LE(nameBuf.length, 26); local.writeUInt16LE(0, 28);
    const localOff = offset;
    await write(local); await write(nameBuf);
    if (data.fromFile) {
      for await (const chunk of createReadStream(data.fromFile)) await write(chunk);
    } else {
      await write(data.fromBuffer);
    }
    central.push({ name: nameBuf, crc, size, localOff });
  }

  // 1. Pristine entries, byte-for-byte payloads.
  for (const e of srcEntries) {
    if (e.method !== 0) throw new Error(`source entry ${e.name} is not stored (method ${e.method}); this packager only mirrors stored archives`);
    await addStored(e.name, { fromBuffer: srcFh.subarray(e.dataOff, e.dataOff + e.usize) });
  }
  // 2. Seed files from the merged harvest, in curation order.
  for (const [harvestPath, archivePath] of seeds) {
    await addStored(archivePath, { fromFile: harvested.get(harvestPath).file });
  }

  // Central directory + EOCD.
  const cdStart = offset;
  for (const c of central) {
    const rec = Buffer.alloc(46);
    rec.writeUInt32LE(0x02014b50, 0); rec.writeUInt16LE(20, 4); rec.writeUInt16LE(20, 6);
    rec.writeUInt16LE(0x0800, 8); rec.writeUInt16LE(0, 10);
    rec.writeUInt16LE(DOS_TIME, 12); rec.writeUInt16LE(DOS_DATE, 14);
    rec.writeUInt32LE(c.crc, 16); rec.writeUInt32LE(c.size, 20); rec.writeUInt32LE(c.size, 24);
    rec.writeUInt16LE(c.name.length, 28);
    rec.writeUInt32LE(EXT_ATTR, 38); rec.writeUInt32LE(c.localOff, 42);
    await write(rec); await write(c.name);
  }
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(central.length, 8); eocd.writeUInt16LE(central.length, 10);
  eocd.writeUInt32LE(offset - cdStart, 12); eocd.writeUInt32LE(cdStart, 16);
  await write(eocd);
  await new Promise(res => ws.end(res));

  // Verification pass: every seed in the produced archive hashes back to
  // its harvest file, and the pristine payloads hash to the source's.
  const produced = await readCentralDirectory(out);
  const outFh = await readFile(out);
  const report = { source, out, captures: captureDirs, entries: [] };
  const byArchivePath = new Map([...seeds].map(([hp, ap]) => [ap, harvested.get(hp)]));
  for (const e of produced) {
    const bytes = outFh.subarray(e.dataOff, e.dataOff + e.usize);
    const sha256 = createHash('sha256').update(bytes).digest('hex');
    report.entries.push({ name: e.name, bytes: e.usize, sha256 });
    const seed = byArchivePath.get(e.name);
    if (seed && sha256 !== seed.sha256) throw new Error(`verification failed: ${e.name} in the new archive does not match the harvest`);
  }
  for (const e of srcEntries) {
    const inNew = report.entries.find(r => r.name === e.name);
    const srcHash = createHash('sha256').update(srcFh.subarray(e.dataOff, e.dataOff + e.usize)).digest('hex');
    if (!inNew || inNew.sha256 !== srcHash) throw new Error(`verification failed: pristine entry ${e.name} changed bytes`);
  }
  report.totalBytes = (await stat(out)).size;
  const reportPath = join(captureDirs[captureDirs.length - 1], 'package-report.json');
  await writeFile(reportPath, JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ out, totalBytes: report.totalBytes, seedCount: seeds.size, entries: report.entries.length, report: reportPath }, null, 2));
}

await main();
