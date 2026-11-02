import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createContext, runInContext, runInNewContext } from 'node:vm';

// FD-77 remainder: the launcher's stall watchdog surfaces a booted,
// no-window, no-progress session as a reported failure in a default
// (unarmed) session, using the guest thread dump's per-process line as
// the signal. These tests extract the watchdog's blocks from
// wine64-launcher.js and run them with stubbed launcher globals (the
// guest-diagnostics.test.mjs pattern).

const launcher = readFileSync(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url), 'utf8');

test('stall watchdog arms BW_THREAD_DUMP only in default sessions', () => {
  const start = launcher.indexOf('if (param("stallwatch") !== "0"');
  assert.ok(start >= 0, 'stall watchdog env block is missing');
  const end = launcher.indexOf('}', launcher.indexOf('ENV["BW_THREAD_DUMP"] = "1";', start));
  assert.ok(end > start, 'stall watchdog env block is incomplete');
  const body = launcher.slice(start, end + 1);
  const envFor = params => {
    const ENV = {};
    runInNewContext(body, { ENV, param: name => (name in params ? params[name] : null) });
    return ENV;
  };
  assert.equal(envFor({}).BW_THREAD_DUMP, '1', 'default session arms the dump export');
  assert.equal(envFor({ stallwatch: '0' }).BW_THREAD_DUMP, undefined, '?stallwatch=0 opts out');
  assert.equal(envFor({ bw64diag: 'threads' }).BW_THREAD_DUMP, undefined, 'bw64diag rigs arm their own dumps');
  assert.equal(envFor({ guestdiag: '10' }).BW_THREAD_DUMP, undefined, 'guestdiag rigs arm their own dumps');
  assert.equal(envFor({ guestdiag: '301' }).BW_THREAD_DUMP, '1', 'out-of-range guestdiag does not count as armed');
  assert.equal(envFor({ guestdiag: 'invalid' }).BW_THREAD_DUMP, '1', 'invalid guestdiag does not count as armed');
});

const SECTION_START = '// --- session stall watchdog (FD-77 remainder)';
const SECTION_END = '// --- end session stall watchdog';

function dumpLines({ pid = 42, running = 9, min = 90000, inflight = 0 } = {}) {
  return [
    'BW_THREAD_DUMP begin maxTid=120',
    `BW_THREAD_DUMP pid=${pid} tid=100 state=running syscall=-(#0) args=0x0,0x0,0x0,0x0,0x0,0x0 rip=0x7f0 wait=- eax=0x0 noprogress_ms=${min} fstage=mem-lock fstage_ms=${min} fpage=0x8dd3`,
    `BW_THREAD_DUMP pid=${pid} tid=101 state=blocked syscall=read(#0) args=0xc,0x0,0x0,0x0,0x0,0x0 rip=0x7f1 wait=KUnixSocketObject::lockCond eax=0x0 noprogress_ms=0 fstage=none fstage_ms=0 fpage=0x0`,
    `BW_THREAD_DUMP proc pid=${pid} running=${running} min_noprogress_ms=${min} memmutex_owner_tid=100 memmutex_owner_ms=${min}`,
    `BW_THREAD_DUMP proxyfetch inflight=${inflight} oldest_tid=0 oldest_ms=0`,
    'BW_THREAD_DUMP ramalloc owner_tid=0 owner_ms=0',
    'BW_THREAD_DUMP end dumped=70 skipped-lock=0',
  ];
}

function makeWatchdog(params = {}) {
  const start = launcher.indexOf(SECTION_START);
  const end = launcher.indexOf(SECTION_END, start);
  assert.ok(start >= 0 && end > start, 'stall watchdog section is missing');
  const section = launcher.slice(start, end);
  const forwarded = [];   // lines Module.print would forward to the session log
  const reported = [];    // bwRuntimeError messages
  const runtimeLogs = []; // direct bwRuntimeLog calls (the evidence flush + FAIL line)
  const warns = [];
  const timeouts = [];
  const intervals = [];
  let shape = { pid: 42, running: 9, min: 90000, inflight: 0 };
  let dumpRc = 1;
  let dumpCalls = 0;
  const state = {
    param: name => (name in params ? params[name] : null),
    console: { log() {}, warn: (...a) => warns.push(a.join(' ')), error() {} },
    window: {
      bwRuntimeLog: line => runtimeLogs.push(line),
      bwRuntimeError: message => reported.push(message),
      setTimeout: (cb, ms) => { timeouts.push({ cb, ms }); return timeouts.length; },
      clearTimeout: () => {},
      setInterval: (cb, ms) => { intervals.push({ cb, ms }); return intervals.length + 1000; },
      clearInterval: () => {},
    },
    liveModule: {
      ccall: name => {
        assert.equal(name, 'bw64_dump_guest_threads');
        dumpCalls++;
        if (dumpRc === 1) for (const line of dumpLines(shape)) print(line);
        return dumpRc;
      },
    },
    sessionDesktopHwndOk: false,
    sessionX11Accepts: 0,
    sessionExplorerOpened: false,
  };
  const ctx = createContext(state);
  runInContext(section, ctx);
  // Mirror Module.print: the line reaches the session log only when the
  // watchdog's line hook lets it through.
  function print(line) { if (ctx.stallWatchLine(line)) forwarded.push(line); }
  return {
    ctx, forwarded, reported, runtimeLogs, warns, timeouts, intervals, print,
    setShape: next => { shape = next; },
    setDumpRc: next => { dumpRc = next; },
    dumpCalls: () => dumpCalls,
    boot() { state.sessionDesktopHwndOk = true; state.sessionX11Accepts = 5; state.sessionExplorerOpened = true; },
    start() {
      ctx.stallWatchStart();
      assert.equal(timeouts.length, 1, 'watchdog schedules its first dump');
      timeouts[0].cb();
      assert.equal(intervals.length, 1, 'watchdog schedules its dump interval');
    },
    tick() { intervals[0].cb(); },
  };
}

test('a booted, windowless, no-progress session is reported as failed with its evidence preserved', () => {
  const w = makeWatchdog();
  w.boot();
  w.start(); // first dump: stalled shape, count 1 — no report yet
  assert.equal(w.dumpCalls(), 1);
  assert.deepEqual(w.reported, []);
  // The dump's summary lines were forwarded; its per-thread lines were held.
  assert.ok(w.forwarded.some(l => l.startsWith('BW_THREAD_DUMP proc pid=42')), 'proc line forwarded');
  assert.ok(!w.forwarded.some(l => l.startsWith('BW_THREAD_DUMP pid=42 tid=')), 'per-thread lines held while healthy');
  w.tick(); // second consecutive stalled dump: report
  assert.equal(w.dumpCalls(), 2);
  assert.equal(w.reported.length, 1);
  assert.match(w.reported[0], /never opened a window and stopped making progress/);
  assert.match(w.reported[0], /reported as failed/);
  const fail = w.runtimeLogs.find(l => l.startsWith('BW64-FAIL '));
  assert.ok(fail, 'a BW64-FAIL line is on the session log');
  assert.match(fail, /reason=booted-no-window-no-progress/);
  assert.match(fail, /pid=42/);
  assert.match(fail, /min_noprogress_ms=90000/);
  // The held per-thread evidence was flushed ahead of the failure.
  assert.ok(w.runtimeLogs.some(l => l.includes('BW_THREAD_DUMP pid=42 tid=100')), 'held dump lines flushed as evidence');
  assert.ok(w.runtimeLogs.indexOf(w.runtimeLogs.find(l => l.includes('tid=100'))) < w.runtimeLogs.indexOf(fail),
    'evidence precedes the failure line');
  // A third stalled dump changes nothing: the failure is reported once.
  for (const line of dumpLines()) w.print(line);
  assert.equal(w.reported.length, 1);
});

test('a slow-but-alive start raises no alarm', () => {
  const w = makeWatchdog();
  w.boot();
  w.setShape({ pid: 42, running: 9, min: 4000, inflight: 0 }); // threads keep executing guest ops
  w.start();
  w.tick(); w.tick();
  assert.deepEqual(w.reported, []);
  // One stalled dump is not proof either.
  w.setShape({ pid: 42, running: 9, min: 90000, inflight: 0 });
  w.tick();
  assert.deepEqual(w.reported, []);
  // Then the window maps: the watchdog stands down for good.
  w.print('XWire: first window mapped (present sink=headless)');
  w.tick(); w.tick();
  assert.deepEqual(w.reported, []);
});

test('a lazy fetch in flight explains the wait and resets the count', () => {
  const w = makeWatchdog();
  w.boot();
  w.setShape({ pid: 42, running: 9, min: 90000, inflight: 3 });
  w.start(); w.tick(); w.tick();
  assert.deepEqual(w.reported, [], 'fetch in flight never convicts');
  w.setShape({ pid: 42, running: 9, min: 90000, inflight: 0 });
  w.tick();
  assert.deepEqual(w.reported, [], 'first clean stalled dump after the fetch only counts once');
  w.tick();
  assert.equal(w.reported.length, 1, 'second consecutive clean stalled dump reports');
});

test('no report before the desktop handshake completes, and small processes never convict', () => {
  const w = makeWatchdog();
  w.start(); w.tick(); w.tick(); // stalled dumps, but the session has not booted
  assert.deepEqual(w.reported, []);
  w.boot();
  w.setShape({ pid: 7, running: 2, min: 90000, inflight: 0 }); // too small to call a pile-up
  w.tick(); w.tick(); w.tick();
  assert.deepEqual(w.reported, []);
  w.setShape({ pid: 42, running: 9, min: 90000, inflight: 0 });
  w.tick();
  assert.deepEqual(w.reported, []);
  w.tick();
  assert.equal(w.reported.length, 1);
});

test('diagnostics-armed and opted-out sessions never start the watchdog', () => {
  for (const params of [{ guestdiag: '10' }, { bw64diag: 'threads' }, { stallwatch: '0' }]) {
    const w = makeWatchdog(params);
    w.ctx.stallWatchStart();
    assert.equal(w.timeouts.length, 0, JSON.stringify(params));
    assert.equal(w.ctx.stallWatchLine('BW_THREAD_DUMP proc pid=42 running=9 min_noprogress_ms=90000'), true,
      'lines are never held when the watchdog is off');
  }
});

test('an unavailable dump is a skipped sample, not evidence', () => {
  const w = makeWatchdog();
  w.boot();
  w.setDumpRc(2); // log lock held across a proxied call: dump not taken
  w.start(); w.tick();
  assert.ok(w.warns.some(line => line.includes('stallwatch')), 'the skip is logged');
  assert.deepEqual(w.reported, []);
  w.setDumpRc(1);
  w.tick();
  assert.deepEqual(w.reported, [], 'the first taken stalled dump only counts once');
  w.tick();
  assert.equal(w.reported.length, 1);
});

test('the watchdog is wired into the runtime lifecycle and both print paths', () => {
  assert.match(launcher, /stallWatchStart\(\);/, 'onRuntimeInitialized starts the watchdog');
  const calls = launcher.match(/stallWatchLine\(text\)/g) || [];
  assert.ok(calls.length >= 2, 'Module print and printErr both pass lines through the watchdog hook');
  assert.ok(launcher.includes('if (param("unimpldump") === "1") ENV["BW64_UNIMPLDUMP"] = "1";'),
    'the probe rigs\u2019 launcher patch marker is intact');
});
