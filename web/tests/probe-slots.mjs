// probe-slots.mjs — slot-based probe concurrency control.
//
// Replaces the exclusive /tmp/bw64-probe.lock with N atomic slots so up to
// PROBE_SLOTS probes can run concurrently. Backward compatible: if a legacy
// /tmp/bw64-probe.lock is held by a live process, we wait for it.
//
// Usage:
//   import { acquireProbeSlot } from './probe-slots.mjs';
//   const slot = await acquireProbeSlot();   // waits for a free slot
//   try { ...probe... } finally { await slot.release(); }
//
// Env:
//   PROBE_SLOTS   number of concurrent slots (default 1)
//   PROBE_SLOT_DIR slot dir (default /tmp/bw64-probe-slots)
import { openSync, closeSync, writeFileSync, unlinkSync, existsSync, mkdirSync, readFileSync } from 'node:fs';
import { setTimeout as sleep } from 'node:timers/promises';

const SLOT_DIR = process.env.PROBE_SLOT_DIR || '/tmp/bw64-probe-slots';
const LEGACY_LOCK = '/tmp/bw64-probe.lock';
// A second CPU-heavy emulator can delay Wine startup and input delivery past
// wall-clock deadlines. Use explicit concurrency only for a suitable host.
export const MAX_SLOTS = Number(process.env.PROBE_SLOTS || 1);

function pidAlive(pid) {
  if (!pid || !Number.isFinite(pid)) return false;
  try { process.kill(pid, 0); return true; } catch { return false; }
}

// True if a legacy-lock holder is still alive. Cleans up stale legacy locks.
function legacyLockHeld() {
  if (!existsSync(LEGACY_LOCK)) return false;
  let pid = 0;
  try { pid = Number(readFileSync(LEGACY_LOCK, 'utf8').trim()); } catch { return false; }
  if (pidAlive(pid)) return true;
  try { unlinkSync(LEGACY_LOCK); } catch {}
  return false;
}

export async function acquireProbeSlot({ timeoutMs = 900000 } = {}) {
  mkdirSync(SLOT_DIR, { recursive: true });
  const t0 = Date.now();
  for (;;) {
    if (!legacyLockHeld()) {
      for (let i = 0; i < MAX_SLOTS; i++) {
        const p = `${SLOT_DIR}/slot-${i}`;
        try {
          const fd = openSync(p, 'wx', 0o644); // O_CREAT|O_EXCL: atomic
          try { writeFileSync(fd, `${process.pid}\n`); } finally { closeSync(fd); }
          let released = false;
          return {
            index: i,
            release: async () => { if (!released) { released = true; try { unlinkSync(p); } catch {} } },
          };
        } catch (e) {
          if (e.code !== 'EEXIST') throw e;
          // Slot file exists: steal it if the owner is dead.
          try {
            const pid = Number(readFileSync(p, 'utf8').trim());
            if (!pidAlive(pid)) { try { unlinkSync(p); } catch {} }
          } catch {}
        }
      }
    }
    if (Date.now() - t0 > timeoutMs) {
      throw new Error(`timed out waiting for a probe slot (slots=${MAX_SLOTS}, dir=${SLOT_DIR})`);
    }
    await sleep(2000);
  }
}
