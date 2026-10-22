// build-lock.mjs — build coordination for probe scripts.
//
// A WASM rebuild replaces dist/runtime/boxedwine64.wasm+js mid-probe, which
// corrupts the run under test ("binary of unknown provenance"). Builds are
// serialized through /tmp/bw64-build.lock.d (see tools/bw64-build.sh and
// BUILD_PROTOCOL.md). Probes MUST wait for build idle before launching the
// browser.
//
// Usage:
//   import { waitForBuildIdle, buildInProgress } from './build-lock.mjs';
//   await waitForBuildIdle();   // throws after timeoutMs if a build is stuck
//   ... start browser ...
//
// Env:
//   BUILD_LOCK_DIR   lock dir (default /tmp/bw64-build.lock.d)

import { existsSync, readFileSync, unlinkSync, rmdirSync } from 'node:fs';
import { setTimeout as sleep } from 'node:timers/promises';

const LOCK_DIR = process.env.BUILD_LOCK_DIR || '/tmp/bw64-build.lock.d';
const INFO_FILE = `${LOCK_DIR}/info`;

function pidAlive(pid) {
  if (!Number.isSafeInteger(pid) || pid <= 0) return false;
  try { process.kill(pid, 0); return true; } catch (error) { return error.code === 'EPERM'; }
}

function readInfo() {
  try {
    const raw = readFileSync(INFO_FILE, 'utf8').trim().split('|');
    if (!/^[1-9][0-9]*$/.test(raw[0]) || !/^[0-9]+$/.test(raw[1]) || raw.length < 3) return null;
    const pid = Number(raw[0]);
    if (!Number.isSafeInteger(pid) || pid > 2147483647) return null;
    return { pid, startedMs: Number(raw[1]) * 1000, owner: raw.slice(2).join('|') };
  } catch { return null; }
}

// True if a build currently holds the lock. Cleans up stale locks whose
// owner is provably dead. Missing info may be a new builder between mkdir and
// writing info; malformed locks also require investigation and remain busy.
export function buildInProgress() {
  if (!existsSync(LOCK_DIR)) return false;
  const info = readInfo();
  if (!info) return true;
  if (!pidAlive(info.pid)) {
    try {
      // Remove only the validated info and an empty directory, never arbitrary
      // contents or a newly acquired owner's lock directory recursively.
      if (readInfo()?.pid !== info.pid) return true;
      unlinkSync(INFO_FILE);
      rmdirSync(LOCK_DIR);
      return false;
    } catch { return existsSync(LOCK_DIR); }
  }
  return true;
}

export function buildLockInfo() {
  if (!existsSync(LOCK_DIR)) return null;
  return readInfo();
}

export function defaultBuildLockTimeoutMs(value = process.env.BUILD_LOCK_TIMEOUT_MS) {
  if (value == null || value === '') return 600000;
  const parsed = Number(value);
  return Number.isSafeInteger(parsed) && parsed > 0 ? parsed : 600000;
}

// Wait until no build holds the lock. Throws on timeout so a stuck build
// fails loudly instead of hanging the probe forever.
export async function waitForBuildIdle({ timeoutMs = defaultBuildLockTimeoutMs(), pollMs = 5000 } = {}) {
  const t0 = Date.now();
  let warned = false;
  for (;;) {
    if (!buildInProgress()) return;
    const info = buildLockInfo();
    if (!warned) {
      console.error(`[build-lock] waiting for build to finish (pid=${info?.pid} owner=${info?.owner})...`);
      warned = true;
    }
    if (Date.now() - t0 > timeoutMs) {
      throw new Error(
        `[build-lock] timed out after ${timeoutMs}ms waiting for build lock ` +
        `(pid=${info?.pid} owner=${info?.owner}). Investigate the stuck build; ` +
        `do NOT start the probe against a half-replaced dist/.`
      );
    }
    await sleep(pollMs);
  }
}
