// probe-guest-death.mjs — guest-death detection + METRICS present extraction
// for Playwright probes. Pure functions only (no Playwright import) so the
// logic is unit-testable in plain node:test.
//
// Why this exists (2026-10-27 boundary window):
//  - Doom A (shipped build) died at ~166s (Wine session teardown) but the
//    probe waited out its full 300s mapping timeout: ~130s wasted.
//  - Gang Beasts died at ~622s but its probe waited the full 3600s mapping
//    timeout: ~50 min wasted.
//  - The wasm worker death (Doom B, RuntimeError memory-access-out-of-bounds)
//    likewise went unnoticed until the record phase ended.
// Death is therefore a first-class probe verdict, not a timeout.
//
// Two death channels:
//  1. WORKER (immediate): a pageerror or console error line showing the wasm
//     worker threw (RuntimeError / memory access out of bounds / worker sent
//     an error). The worker is gone; nothing further can happen.
//  2. TEARDOWN + SILENCE: a Wine session-daemon exit (services.exe or
//     explorer.exe exit_group/exit) followed by SILENCE_AFTER_TEARDOWN_MS with
//     no guest-activity console line. The daemon exit alone is NOT death:
//     services.exe exits early in healthy Gang Beasts runs too (116-128s)
//     while the game lives on. What distinguishes death is that NOTHING
//     guest-side ever speaks again. The emulator heartbeat (`persist: synced`
//     every ~5s) is NOT guest activity and must not reset the clock — after
//     Doom A died, persist lines were the only console traffic for 130+s.
//
// Also in this module: extractMetricsPresent(), because `bw64: METRICS`
// console lines are truncated at ~832 payload chars in play phase (the
// guest JSON writer is fine — the console channel cuts it), which makes the
// full JSON unparseable, while the `present` object sits early in the line
// and survives the cut. DIB games (Doom) present through SDL, never through
// vkQueuePresentKHR, so the activity gate must count SDL presents from
// these sections or it burns its whole wait (Doom B burned ~295s).

export const SILENCE_AFTER_TEARDOWN_MS = 45000;

// Lines that are emulator-owned heartbeat, not guest life.
const HEARTBEAT_RES = [/^persist: synced/];
// Browser chrome noise, not guest life.
const BROWSER_NOISE_RES = [
  /ScriptProcessorNode is deprecated/,
  /DevTools listening on/,
];

export function isGuestActivityLine(text) {
  if (typeof text !== 'string' || !text) return false;
  if (HEARTBEAT_RES.some((re) => re.test(text))) return false;
  if (BROWSER_NOISE_RES.some((re) => re.test(text))) return false;
  return true;
}

// The wasm worker is dead: Doom B's three error lines all match these.
const WORKER_DEATH_RES = [
  /worker sent an error/i,
  /RuntimeError:\s*memory access out of bounds/,
  /memory access out of bounds/,
  /Uncaught RuntimeError/,
];

// A Wine session daemon exiting. The exit_group *trace* lines that follow a
// daemon exit do not name the exe; the syscall line itself does (cmd=).
const TEARDOWN_RES = [
  /CPU64: exit(?:_group)? syscall, status=\d+.*(?:services|explorer)\.exe/,
  /exit syscall, status=\d+.*(?:services|explorer)\.exe/,
];

export function isWorkerDeathText(text) {
  return typeof text === 'string' && WORKER_DEATH_RES.some((re) => re.test(text));
}

export function isTeardownLine(text) {
  return typeof text === 'string' && TEARDOWN_RES.some((re) => re.test(text));
}

export function createGuestDeathWatch({ silenceAfterTeardownMs = SILENCE_AFTER_TEARDOWN_MS } = {}) {
  let teardownAtMs = null;
  let teardownSignature = null;
  let lastTeardownSignature = null;
  let lastGuestActivityMs = null;
  let workerDeath = null;

  function feed(line) {
    if (!line || typeof line.text !== 'string') return;
    const t = typeof line.t === 'number' ? line.t : 0;
    if (line.type === 'error' && isWorkerDeathText(line.text)) {
      if (!workerDeath) workerDeath = { signature: line.text, atMs: t };
      return;
    }
    if (isTeardownLine(line.text)) {
      if (teardownAtMs === null) {
        teardownAtMs = t;
        teardownSignature = line.text;
      }
      // The verdict should cite the FINAL teardown, not the first: services
      // exits early in healthy runs too (snake: 41s/76s/81s, session lived
      // on), while the fatal teardown is the last one (explorer + services
      // at 169-174s, then total silence).
      lastTeardownSignature = line.text;
    }
    if (isGuestActivityLine(line.text)) lastGuestActivityMs = t;
  }

  function feedPageError(msg, t = 0) {
    if (isWorkerDeathText(String(msg)) && !workerDeath) {
      workerDeath = { signature: String(msg), atMs: t };
    }
  }

  // Returns null, or { verdict: 'GUEST-DIED', kind, signature, atMs }.
  function check(nowMs) {
    if (workerDeath) {
      return { verdict: 'GUEST-DIED', kind: 'worker', signature: workerDeath.signature, atMs: workerDeath.atMs };
    }
    if (teardownAtMs !== null && lastGuestActivityMs !== null &&
        nowMs - lastGuestActivityMs >= silenceAfterTeardownMs) {
      return {
        verdict: 'GUEST-DIED', kind: 'teardown-silence',
        signature: lastTeardownSignature || teardownSignature, atMs: lastGuestActivityMs,
      };
    }
    return null;
  }

  function state() {
    return { teardownAtMs, teardownSignature, lastTeardownSignature, lastGuestActivityMs, workerDeath };
  }

  return { feed, feedPageError, check, state };
}

// Extract the `present` object from a `bw64: METRICS {...}` console line,
// even when the line was truncated after the present section (the usual
// play-phase case). Returns { vk, vk32, sdl, count, path } or null when the
// present object is absent or itself cut.
export function extractMetricsPresent(text) {
  if (typeof text !== 'string') return null;
  const at = text.indexOf('"present":{');
  if (at < 0) return null;
  const start = at + '"present":'.length;
  const end = text.indexOf('}', start);
  if (end < 0) return null;
  const body = text.slice(start, end);
  const num = (key) => {
    const m = body.match(new RegExp(`"${key}":(-?\\d+)`));
    return m ? Number(m[1]) : null;
  };
  const pathMatch = body.match(/"path":"([a-z0-9]+)"/);
  const vk = num('vk');
  const vk32 = num('vk32');
  const sdl = num('sdl');
  const count = num('count');
  if (vk === null && vk32 === null && sdl === null && count === null) return null;
  return { vk: vk ?? 0, vk32: vk32 ?? 0, sdl: sdl ?? 0, count: count ?? 0, path: pathMatch ? pathMatch[1] : null };
}
