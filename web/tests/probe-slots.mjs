// probe-slots.mjs — FIFO probe concurrency control for heavy and light work.
//
// Usage:
//   import { acquireProbeSlot } from './probe-slots.mjs';
//   const slot = await acquireProbeSlot();
//   try { ...probe... } finally { await slot.release(); }
//
// Env:
//   PROBE_SLOTS       number of heavy slots (default 2)
//   PROBE_LIGHT_SLOTS number of light slots (default 2)
//   PROBE_KIND        default kind when the call omits kind (default heavy)
//   PROBE_SLOT_TIMEOUT_MS maximum wait (default 4 hours)
//   PROBE_SLOT_DIR    slot directory (default /tmp/bw64-probe-slots)

import {
  closeSync, existsSync, mkdirSync, openSync, readFileSync, readdirSync,
  readlinkSync, realpathSync, statSync, unlinkSync, writeFileSync,
} from 'node:fs';
import { createHash, randomBytes } from 'node:crypto';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { setTimeout as sleep } from 'node:timers/promises';

const SLOT_DIR = process.env.PROBE_SLOT_DIR || '/tmp/bw64-probe-slots';
const LEGACY_LOCK = '/tmp/bw64-probe.lock';
export const MAX_SLOTS = Number(process.env.PROBE_SLOTS || 2);
export const MAX_LIGHT_SLOTS = Number(process.env.PROBE_LIGHT_SLOTS || 2);

const SLOT_COUNTS = { heavy: MAX_SLOTS, light: MAX_LIGHT_SLOTS };
const slotPrefix = kind => kind === 'heavy' ? 'slot-' : 'light-';
const slotPath = (kind, index) => join(SLOT_DIR, `${slotPrefix(kind)}${index}`);
const slotInfoPath = filename => join(SLOT_DIR, `info-${filename}.json`);
const waiterPrefix = kind => `wait-${kind}-`;

function pidFromFile(file) {
  try {
    const pid = Number(readFileSync(file, 'utf8').trim());
    return Number.isSafeInteger(pid) && pid > 0 ? pid : 0;
  } catch {
    return 0;
  }
}

function pidAlive(pid) {
  if (!pid || !Number.isFinite(pid)) return false;
  try { process.kill(pid, 0); return true; } catch { return false; }
}

function unlinkIfPresent(file) {
  try { unlinkSync(file); } catch {}
}

function removeSlotFiles(file) {
  unlinkIfPresent(file);
  unlinkIfPresent(slotInfoPath(basename(file)));
}

// True if a legacy-lock holder is still alive. Cleans up stale legacy locks.
function legacyLockHeld() {
  if (!existsSync(LEGACY_LOCK)) return false;
  let pid = 0;
  try { pid = Number(readFileSync(LEGACY_LOCK, 'utf8').trim()); } catch { return false; }
  if (pidAlive(pid)) return true;
  unlinkIfPresent(LEGACY_LOCK);
  return false;
}

// Tree-scoped coordination (FD-224).
//
// A build replaces only its own tree's dist/, so a build in one tree must
// not block probes in another. Each build publishes its claim under a file
// named for its tree (sha256 of the canonical tree path, first 16 hex
// chars); a probe ignores claims from any other tree. The legacy single
// build-pending file (written by older builds) still blocks every tree,
// fail closed, until it is gone.
//
// The probe's tree is the tree whose dist/ it will test:
//   - BW_TEST_BASE_URL set: the dist is remote (the live site) — no local
//     build can replace it, so no local build blocks the probe.
//   - BW_TEST_DIST / WINE_DIST_DIR set: the dist's owning tree is its
//     parent directory when that parent is a git worktree (has a .git
//     entry); any other dist (the live site dir, a staging copy) is not
//     written by any build, so it is treated as remote.
//   - Otherwise: the tree containing this module (the probe's own tree).
// A null tree means "remote": no local build blocks the probe.
export function moduleTreeRoot() {
  return realpathSync(join(dirname(fileURLToPath(import.meta.url)), '..', '..'));
}

export function probeDistTree() {
  if (process.env.BW_TEST_BASE_URL) return null;
  const distEnv = process.env.BW_TEST_DIST || process.env.WINE_DIST_DIR;
  if (distEnv) {
    const parent = dirname(resolve(distEnv));
    return existsSync(join(parent, '.git')) ? realpathSync(parent) : null;
  }
  return moduleTreeRoot();
}

function claimFileName(tree) {
  return `build-pending-${createHash('sha256').update(tree).digest('hex').slice(0, 16)}`;
}

function claimPid(file) {
  try {
    const pid = Number(readFileSync(file, 'utf8').trim().split('|')[0]);
    return Number.isSafeInteger(pid) && pid > 0 ? pid : 0;
  } catch { return 0; }
}

// True if a build in the probe's tree is draining or writing. A claim from
// another tree, or a probe testing a remote dist, never blocks. The legacy
// single file (older builds) still blocks every tree, fail closed.
export function buildPending(tree = probeDistTree()) {
  const legacy = join(SLOT_DIR, 'build-pending');
  if (existsSync(legacy)) {
    if (pidAlive(pidFromFile(legacy))) return true;
    unlinkIfPresent(legacy);
  }
  if (tree == null) return false;
  const file = join(SLOT_DIR, claimFileName(tree));
  if (!existsSync(file)) return false;
  if (pidAlive(claimPid(file))) return true;
  unlinkIfPresent(file);
  return false;
}

function removeDeadWaiters(kind) {
  const prefix = waiterPrefix(kind);
  const names = readdirSync(SLOT_DIR).filter(name => name.startsWith(prefix)).sort();
  const alive = [];
  for (const name of names) {
    const file = join(SLOT_DIR, name);
    if (!pidAlive(pidFromFile(file))) unlinkIfPresent(file);
    else alive.push(name);
  }
  return alive;
}

function freeSlotCount(kind) {
  let free = 0;
  for (let i = 0; i < SLOT_COUNTS[kind]; i++) {
    const file = slotPath(kind, i);
    if (!existsSync(file)) {
      free++;
      continue;
    }
    if (!pidAlive(pidFromFile(file))) {
      removeSlotFiles(file);
      if (!existsSync(file)) free++;
    }
  }
  return free;
}

function createWaiter(kind) {
  for (;;) {
    const name = `${waiterPrefix(kind)}${String(Date.now()).padStart(15, '0')}-${process.pid}-${randomBytes(3).toString('hex')}`;
    const file = join(SLOT_DIR, name);
    let fd;
    try { fd = openSync(file, 'wx', 0o644); }
    catch (error) {
      if (error.code === 'EEXIST') continue;
      throw error;
    }
    try { writeFileSync(fd, `${process.pid}\n`); }
    finally { closeSync(fd); }
    return { name, file };
  }
}

function createSlot(kind, index) {
  const filename = `${slotPrefix(kind)}${index}`;
  const file = join(SLOT_DIR, filename);
  let fd;
  try { fd = openSync(file, 'wx', 0o644); }
  catch (error) {
    if (error.code === 'EEXIST') return null;
    throw error;
  }

  const startedAt = Date.now();
  const info = {
    pid: process.pid,
    kind,
    cwd: process.cwd(),
    argv: process.argv.slice(1),
    startedAt,
    demo: process.env.DEMO ?? null,
    jit: process.env.JIT ?? null,
    // The tree whose dist/ this probe tests (null: a remote dist no local
    // build can replace). Builds use it to drain only their own tree's
    // probes (FD-224); a missing tree means unknown, fail closed.
    tree: probeDistTree(),
  };
  try {
    writeFileSync(fd, `${process.pid}\n`);
    writeFileSync(slotInfoPath(filename), JSON.stringify(info));
  } catch (error) {
    unlinkIfPresent(file);
    unlinkIfPresent(slotInfoPath(filename));
    throw error;
  } finally {
    closeSync(fd);
  }

  let released = false;
  return {
    index,
    kind,
    release: async () => {
      if (released) return;
      released = true;
      removeSlotFiles(file);
    },
  };
}

function readSidecar(filename) {
  try {
    const value = JSON.parse(readFileSync(slotInfoPath(filename), 'utf8'));
    return value && typeof value === 'object' ? value : null;
  } catch {
    return null;
  }
}

function procDetails(pid) {
  if (!pid) return {};
  const details = {};
  try { details.cwd = readlinkSync(`/proc/${pid}/cwd`); } catch {}
  try {
    const argv = readFileSync(`/proc/${pid}/cmdline`).toString().split('\0').filter(Boolean);
    if (argv.length) details.argv = argv;
  } catch {}
  try {
    const env = readFileSync(`/proc/${pid}/environ`, 'utf8').split('\0');
    const demo = env.find(item => item.startsWith('DEMO='));
    if (demo) details.demo = demo.slice('DEMO='.length);
  } catch {}
  return details;
}

function worktreeName(info, pid) {
  const cwd = info?.cwd || procDetails(pid).cwd;
  return cwd ? basename(cwd) : '?';
}

function demoOrArgv(info, pid) {
  const details = info || procDetails(pid);
  if (details.demo) return String(details.demo);
  const argv0 = details.argv?.[0];
  return argv0 ? basename(argv0) : '?';
}

function ageMinutes(startedAt) {
  const age = Number(startedAt);
  return Number.isFinite(age) && age > 0 ? String(Math.max(0, Math.floor((Date.now() - age) / 60000))) : '?';
}

function holderDescriptions(kind) {
  const holders = [];
  for (let i = 0; i < SLOT_COUNTS[kind]; i++) {
    const filename = `${slotPrefix(kind)}${i}`;
    const file = join(SLOT_DIR, filename);
    if (!existsSync(file)) continue;
    const pid = pidFromFile(file);
    const info = readSidecar(filename);
    holders.push(`${pid || '?'} ${worktreeName(info, pid)} ${demoOrArgv(info, pid)} ${ageMinutes(info?.startedAt)}m`);
  }
  return holders;
}

function logWaiting(kind, queueIndex, queueLength) {
  const holders = holderDescriptions(kind);
  console.error(`[probe-slots] waiting for ${kind} slot: queue position ${queueIndex + 1}/${queueLength}; holders: ${holders.join(', ') || 'none'}`);
}

export async function acquireProbeSlot({ timeoutMs, kind } = {}) {
  kind ??= process.env.PROBE_KIND || 'heavy';
  if (kind !== 'heavy' && kind !== 'light') {
    throw new Error(`invalid probe slot kind: ${kind} (kind must be 'heavy' or 'light')`);
  }
  timeoutMs ??= Number(process.env.PROBE_SLOT_TIMEOUT_MS || 14400000);

  mkdirSync(SLOT_DIR, { recursive: true });
  const startedWaitingAt = Date.now();
  let lastNoticeAt = startedWaitingAt;
  const waiter = createWaiter(kind);
  const removeWaiter = () => unlinkIfPresent(waiter.file);
  const onExit = () => removeWaiter();
  process.once('exit', onExit);

  try {
    for (;;) {
      const queue = removeDeadWaiters(kind);
      const queueIndex = queue.indexOf(waiter.name);
      const free = freeSlotCount(kind);
      if (!legacyLockHeld() && !buildPending() && queueIndex >= 0 && queueIndex < free) {
        for (let i = 0; i < SLOT_COUNTS[kind]; i++) {
          const slot = createSlot(kind, i);
          if (!slot) continue;
          removeWaiter();
          return slot;
        }
      }

      if (Date.now() - startedWaitingAt > timeoutMs) {
        throw new Error(`timed out waiting for a probe slot (slots=${SLOT_COUNTS[kind]}, dir=${SLOT_DIR}, kind=${kind})`);
      }
      if (Date.now() - lastNoticeAt >= 60000) {
        logWaiting(kind, Math.max(0, queueIndex), queue.length);
        lastNoticeAt = Date.now();
      }
      await sleep(2000);
    }
  } finally {
    removeWaiter();
    process.removeListener('exit', onExit);
  }
}

function statusForKind(kind) {
  console.log(`${kind}:`);
  let count = 0;
  for (let i = 0; i < SLOT_COUNTS[kind]; i++) {
    const filename = `${slotPrefix(kind)}${i}`;
    const file = join(SLOT_DIR, filename);
    if (!existsSync(file)) continue;
    count++;
    const pid = pidFromFile(file);
    const alive = pidAlive(pid);
    const info = readSidecar(filename);
    const details = info || procDetails(pid);
    const argv = details.argv?.[0] ? basename(details.argv[0]) : '?';
    let startedAt = details.startedAt;
    if (!startedAt) {
      try { startedAt = statSync(file).mtimeMs; } catch {}
    }
    console.log(`  holder kind=${kind} slot=${filename} pid=${pid || '?'} alive=${alive} worktree=${worktreeName(details, pid)} demo=${details.demo ?? '-'} argv=${argv} age=${ageMinutes(startedAt)}m`);
  }
  const waiterNames = (() => {
    try { return readdirSync(SLOT_DIR).filter(name => name.startsWith(waiterPrefix(kind))).sort(); }
    catch { return []; }
  })();
  for (const filename of waiterNames) {
    count++;
    const file = join(SLOT_DIR, filename);
    const pid = pidFromFile(file);
    const alive = pidAlive(pid);
    const details = procDetails(pid);
    let startedAt;
    try { startedAt = statSync(file).mtimeMs; } catch {}
    const argv = details.argv?.[0] ? basename(details.argv[0]) : '?';
    console.log(`  waiter kind=${kind} slot=${filename} pid=${pid || '?'} alive=${alive} worktree=${worktreeName(details, pid)} demo=${details.demo ?? '-'} argv=${argv} age=${ageMinutes(startedAt)}m`);
  }
  if (!count) console.log('  (none)');
}

function printClaims() {
  let names = [];
  try { names = readdirSync(SLOT_DIR).filter(name => name.startsWith('build-pending')).sort(); }
  catch { return; }
  if (!names.length) {
    console.log('build-pending: (none)');
    return;
  }
  console.log('build-pending:');
  for (const name of names) {
    const file = join(SLOT_DIR, name);
    const pid = claimPid(file);
    let tree = '';
    try { tree = readFileSync(file, 'utf8').trim().split('|').slice(1).join('|'); } catch {}
    console.log(`  ${name} pid=${pid || '?'} alive=${pidAlive(pid)} tree=${tree || '(legacy, all trees)'}`);
  }
}

function printStatus() {
  for (const kind of ['heavy', 'light']) statusForKind(kind);
  printClaims();
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href && process.argv[2] === 'status') {
  printStatus();
}
