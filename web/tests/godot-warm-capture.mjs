// FD-55 pre-warm capture driver (packaging tool, not a check).
//
// Launches the godot-squash demo through the standard shell path on a fresh
// browser profile and lets it run until the guest presents Vulkan frames.
// It then harvests the per-installation persistence store — the exact set of
// files a returning session would restore before boot — so the first-run
// caches the guest wrote can be folded into the demo archive by the
// packaging step (tools/demo-packaging/build-warmed-archive.mjs).
//
// The unmodified demo-matrix remains the demo check; this driver makes no
// pass/fail judgment about the demo. Env knobs:
//   OUT                 output dir (default test-results/fd55prewarm/warm-<stamp>)
//   WARM_DEADLINE_S     give up waiting for presents after this (default 3300)
//   WARM_PRESENT_TARGET cumulative vk presents that end the wait (default 30)
import { chromium } from '@playwright/test';
import { createHash } from 'node:crypto';
import { appendFile, mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serve } from '../../tools/browser/serve.mjs';
import { screenshotPixels } from './suite/lib/pixels.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
process.env.DEMO ??= 'godot-squash';   // demo-matrix.mjs validates DEMO at import time
const { applyJitSetting, launchDemoGame } = await import('./demo-matrix.mjs');

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
process.chdir(repo);
const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/T/, 'T').slice(0, 15);
const outDir = resolve(repo, process.env.OUT || join('test-results/fd55prewarm', `warm-${stamp}`));
const deadlineS = Number(process.env.WARM_DEADLINE_S || 3300);
const presentTarget = Number(process.env.WARM_PRESENT_TARGET || 30);
const PERSIST_DB = 'bw64persist-wine-demo-godot-squash';
const MEMFS_ROOT = '/root';

let slot, server, browser, page;
let startedAt = 0;
const consoleLines = [];
const metrics = [];           // {atSeconds, snapshot}
const events = {};            // named guest milestones -> elapsed seconds
let cumVkPresents = 0;
let cumPresents = 0;

const elapsed = () => (Date.now() - startedAt) / 1000;
const note = (name, at = elapsed()) => { if (!(name in events)) { events[name] = Number(at.toFixed(1)); console.log(`[warm] ${name} at ${events[name]}s`); } };

function parseMetrics(text) {
  const marker = 'bw64: METRICS ';
  const at = text.indexOf(marker);
  if (at < 0) return null;
  try { return JSON.parse(text.slice(at + marker.length).split(/\r?\n/, 1)[0].trim()); } catch { return null; }
}

async function status(extra = {}) {
  const row = { atSeconds: Number(elapsed().toFixed(1)), cumVkPresents, cumPresents, events: { ...events }, ...extra };
  await appendFile(join(outDir, 'status.jsonl'), JSON.stringify(row) + '\n').catch(() => {});
  return row;
}

async function idbManifest() {
  return page.evaluate(async dbName => {
    const db = await new Promise((res, rej) => {
      const rq = indexedDB.open(dbName, 1);
      rq.onsuccess = () => res(rq.result);
      rq.onerror = () => rej(rq.error);
    });
    try {
      const rows = await new Promise((res, rej) => {
        const out = [];
        const cur = db.transaction('files', 'readonly').objectStore('files').openCursor();
        cur.onsuccess = () => {
          const c = cur.result;
          if (!c) { res(out); return; }
          const v = c.value;
          out.push({ path: String(c.key), bytes: v && v.data ? v.data.length : 0, sig: v && v.sig ? String(v.sig) : '' });
          c.continue();
        };
        cur.onerror = () => rej(cur.error);
      });
      return rows;
    } finally { db.close(); }
  }, PERSIST_DB);
}

async function idbReadChunk(path, offset, length) {
  return page.evaluate(async ({ dbName, path, offset, length }) => {
    const db = await new Promise((res, rej) => {
      const rq = indexedDB.open(dbName, 1);
      rq.onsuccess = () => res(rq.result);
      rq.onerror = () => rej(rq.error);
    });
    try {
      const rec = await new Promise((res, rej) => {
        const rq = db.transaction('files', 'readonly').objectStore('files').get(path);
        rq.onsuccess = () => res(rq.result);
        rq.onerror = () => rej(rq.error);
      });
      if (!rec || !rec.data) return null;
      const slice = rec.data.subarray(offset, offset + length);
      let bin = '';
      const step = 32768;
      for (let i = 0; i < slice.length; i += step) bin += String.fromCharCode(...slice.subarray(i, i + step));
      return btoa(bin);
    } finally { db.close(); }
  }, { dbName: PERSIST_DB, path, offset, length });
}

async function harvest() {
  // Wait for the persistence store to stop changing (two identical manifests
  // 20 s apart) so a cache flush in flight is not captured half-written.
  let manifest = await idbManifest();
  for (let i = 0; i < 4; i++) {
    await page.waitForTimeout(20000);
    const next = await idbManifest();
    const same = JSON.stringify(next.map(r => [r.path, r.bytes, r.sig])) === JSON.stringify(manifest.map(r => [r.path, r.bytes, r.sig]));
    manifest = next;
    if (same) break;
  }
  await writeFile(join(outDir, 'persist-manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  const harvestDir = join(outDir, 'harvest');
  const files = [];
  for (const row of manifest) {
    if (!row.path.startsWith(MEMFS_ROOT + '/home/username/')) continue;
    const rel = row.path.slice(MEMFS_ROOT.length + 1);   // home/username/...
    const chunks = [];
    for (let off = 0; off < row.bytes; off += 1024 * 1024) {
      const b64 = await idbReadChunk(row.path, off, Math.min(1024 * 1024, row.bytes - off));
      if (b64 === null) throw new Error(`persist entry vanished during harvest: ${row.path}`);
      chunks.push(Buffer.from(b64, 'base64'));
    }
    const data = Buffer.concat(chunks);
    if (data.length !== row.bytes) throw new Error(`harvest size mismatch for ${row.path}: ${data.length} != ${row.bytes}`);
    const dest = join(harvestDir, rel);
    await mkdir(dirname(dest), { recursive: true });
    await writeFile(dest, data);
    files.push({ path: rel, bytes: data.length, sha256: createHash('sha256').update(data).digest('hex') });
    console.log(`[warm] harvested ${rel} (${data.length} bytes)`);
  }
  await writeFile(join(outDir, 'harvest-manifest.json'), JSON.stringify(files, null, 2) + '\n');
  return files;
}

async function displayPixels() {
  const box = await page.locator('#display').boundingBox().catch(() => null);
  if (!box) return null;
  const clip = { x: Math.max(0, Math.floor(box.x)), y: Math.max(0, Math.floor(box.y)), width: Math.max(1, Math.ceil(box.width)), height: Math.max(1, Math.ceil(box.height)) };
  try { return screenshotPixels(await page.screenshot({ type: 'png', clip })); } catch { return null; }
}

async function main() {
  await mkdir(outDir, { recursive: true });
  await waitForBuildIdle();
  slot = await acquireProbeSlot();
  const build = JSON.parse(await readFile(join(repo, 'dist/runtime/BUILD_INFO.json'), 'utf8'));
  if (!process.env.BW_TEST_BASE_URL) server = await serve(process.env.BW_TEST_DIST || 'dist');
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
    headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
  });
  page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  startedAt = Date.now();
  page.on('console', msg => {
    const text = msg.text();
    consoleLines.push(`[${elapsed().toFixed(1)}s] ${msg.type()}: ${text}`);
    const snap = parseMetrics(text);
    if (snap) {
      metrics.push({ atSeconds: elapsed(), snapshot: snap });
      cumVkPresents += Number(snap.present?.vk || 0);
      cumPresents += Number(snap.present?.count || 0);
    }
    if (text.includes('Godot Engine v')) note('godotBanner');
    if (text.includes('vkCreateSwapchainKHR')) note('swapchainCreated');
    if (text.includes('vkQueuePresentKHR')) note('firstPresentTrap');
    if (text.includes('first window mapped')) note('windowMapped');
  });
  page.on('pageerror', err => consoleLines.push(`[${elapsed().toFixed(1)}s] pageerror: ${err.stack || err.message}`));

  const baseUrl = process.env.BW_TEST_BASE_URL || `http://127.0.0.1:${server.address().port}/`;
  await page.goto(baseUrl, { waitUntil: 'domcontentloaded' });
  await applyJitSetting(page, '0');
  await launchDemoGame(page, 'godot-squash', { mappedTimeout: 1200 });
  note('launchReturned');

  let outcome = 'deadline';
  let lastShot = 0;
  while (elapsed() < deadlineS) {
    await page.waitForTimeout(10000);
    await status();
    if (cumVkPresents > 0) note('firstVkPresentMetric');
    if (elapsed() - lastShot >= 300) {
      lastShot = elapsed();
      const px = await displayPixels().catch(() => null);
      await page.screenshot({ path: join(outDir, `warm-${Math.round(elapsed())}s.png`), fullPage: true }).catch(() => {});
      await status({ pixels: px });
    }
    if (cumVkPresents >= presentTarget) { outcome = 'presenting'; break; }
  }
  await status({ outcome });
  await page.screenshot({ path: join(outDir, 'warm-final.png'), fullPage: true }).catch(() => {});
  const finalPixels = await displayPixels().catch(() => null);
  const files = await harvest();
  const summary = {
    driver: 'godot-warm-capture', build, outcome, deadlineS, presentTarget,
    elapsedSeconds: Number(elapsed().toFixed(1)), cumVkPresents, cumPresents, events,
    finalPixels, harvestedFiles: files.length,
    harvestedBytes: files.reduce((n, f) => n + f.bytes, 0),
  };
  await writeFile(join(outDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
  console.log(JSON.stringify({ ...summary, outDir: relative(repo, outDir) }));
}

try { await main(); }
finally {
  if (page) await writeFile(join(outDir, 'console.txt'), consoleLines.join('\n') + '\n').catch(() => {});
  await browser?.close().catch(() => {});
  if (server) await new Promise(r => server.close(r)).catch(() => {});
  await slot?.release();
}
