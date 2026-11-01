import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

// Execute the launcher's actual predicate with its native/session boundary.
const source = readFileSync(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url), 'utf8');
const predicate = source.match(/function sessionReady\(\) \{[\s\S]*?\n    \}/)?.[0];
assert.ok(predicate, 'Runtime readiness predicate must exist');
function ready(session, pinsReady, nativeReady, desktopReady = false) {
  return runInNewContext(predicate + '; sessionReady()', {
    SESSION: session, sessionPinsReady: pinsReady, sessionDesktopReady: desktopReady,
    sessionReadyRaw: () => nativeReady,
  });
}

test('a standalone 32-bit game is ready without a persistent-session wineboot helper', () => {
  assert.equal(ready(false, false, true), true);
});
test('a persistent session waits for a working desktop after its pin spawns', () => {
  assert.equal(ready(true, false, true), false);
  assert.equal(ready(true, true, true), false);
  assert.equal(ready(true, true, true, true), true);
});
// FD-47: desktop readiness is the whole startup handshake, not the
// pin's READY line alone. Until 2026-10-31 the observer accepted any
// hex value as a window handle; on a build whose win32u syscalls are
// never dispatched (FD-40) the pin's window creation "returns" the
// NTSTATUS error 0xC000000D, and sessions with no desktop at all were
// certified as started (FD-43).
const observerSource = source.match(/function observeSessionOutput\(text\) \{[\s\S]*?\n    \}/)?.[0];
assert.ok(observerSource, 'Session output observer must exist');
function observerHarness() {
  const errors = [];
  const state = {
    sessionDesktopReady: false, sessionDesktopHwndOk: false,
    sessionX11Accepts: 0, sessionExplorerOpened: false, sessionHwndRejected: {},
    console: { error: text => errors.push(text), log() {}, warn() {} },
  };
  return {
    state, errors,
    feed(text) { runInNewContext(observerSource + `; observeSessionOutput(${JSON.stringify(text)})`, state); },
    feedX11(n) {
      for (let i = 0; i < n; i++) {
        this.x11fed = (this.x11fed || 0) + 1;
        this.feed(`log: XWireServer: accepted X11 client connection (now ${this.x11fed} open)`);
      }
    },
    feedExplorer() {
      this.feed("log: sys_openat64: OPENED '/home/username/.wine/dosdevices/c:/windows/system32/explorer.exe' -> fd 67");
    },
  };
}

test('an NTSTATUS error value in place of a handle never certifies the desktop', () => {
  const h = observerHarness();
  h.feed('[guest fd=1 pid=14] deskpin: READY hwnd=0xc000000d');
  assert.equal(h.state.sessionDesktopReady, false);
  assert.equal(h.state.sessionDesktopHwndOk, false);
  assert.equal(h.errors.length, 1);
  assert.match(h.errors[0], /BW64-FAIL/);
  // Even with the rest of the handshake present, the error value
  // itself must not become the certifying handle.
  h.feedX11(5);
  h.feedExplorer();
  assert.equal(h.state.sessionDesktopReady, false);
});

test('zero and warning-class values are not handles either', () => {
  for (const bad of ['0x0', '0x80000003']) {
    const h = observerHarness();
    h.feed(`deskpin: READY hwnd=${bad}`);
    h.feedX11(5);
    h.feedExplorer();
    assert.equal(h.state.sessionDesktopReady, false, `${bad} must not certify`);
    assert.equal(h.errors.length, 1);
  }
});

test('a real handle alone does not certify: the X11 and explorer handshake is required', () => {
  const h = observerHarness();
  h.feed('wine64session: spawned pid=14');
  assert.equal(h.state.sessionDesktopReady, false);
  h.feed('[guest fd=1 pid=14] deskpin: READY hwnd=0x10040');
  assert.equal(h.state.sessionDesktopReady, false);
  h.feedExplorer();
  h.feedX11(4);
  assert.equal(h.state.sessionDesktopReady, false);
  h.feedX11(1);
  assert.equal(h.state.sessionDesktopReady, true);
});

test('without the explorer launch the desktop is not certified either', () => {
  const h = observerHarness();
  h.feed('[guest fd=1 pid=14] deskpin: READY hwnd=0x10040');
  h.feedX11(5);
  assert.equal(h.state.sessionDesktopReady, false);
  h.feedExplorer();
  assert.equal(h.state.sessionDesktopReady, true);
});

test('the handshake evidences may arrive in any order', () => {
  const h = observerHarness();
  h.feedExplorer();
  h.feedX11(5);
  assert.equal(h.state.sessionDesktopReady, false);
  h.feed('[guest fd=1 pid=14] deskpin: READY hwnd=0x10048');
  assert.equal(h.state.sessionDesktopReady, true);
});

const pinFunction = source.slice(source.indexOf('    function pinSessionServerWhenReady()'),
  source.indexOf('    // --- public: switch to another app'));
function pinHarness(arch32, { nativeReady = true, spawnSuccess = true } = {}) {
  const timers = [], spawns = [], errors = [];
  const state = {
    ARCH32: arch32, WINESERVER64: arch32 ? 'wineserver32' : 'wineserver64',
    sessionServerPinned: false, sessionPinsReady: false, currentAppPid: 0, pid: 10,
    console: { log() {}, warn() {}, error(text) { errors.push(text); } },
    sessionReadyRaw: () => nativeReady,
    lastSpawnedPid: () => state.pid,
    wineProgArgv: exe => ['wine', exe],
    spawnArgv: argv => { spawns.push(argv); return spawnSuccess; },
    setTimeout: fn => timers.push(fn),
    callExport: name => { throw new Error('Unexpected native protocol workaround: ' + name); },
  };
  runInNewContext(pinFunction + '; pinSessionServerWhenReady()', state);
  return { state, spawns, errors, tick() { assert.ok(timers.length); timers.shift()(); },
    exhaust() { for (let i = 0; timers.length && i < 1000; ++i) timers.shift()(); assert.equal(timers.length, 0); } };
}
for (const arch32 of [true, false]) test(`${arch32 ? 'x86' : 'x64'} session pins drain in order without forged Wine requests`, () => {
  const h = pinHarness(arch32);
  assert.equal(h.state.currentAppPid, 10);
  assert.equal(h.spawns.length, 1);
  assert.deepEqual(Array.from(h.spawns[0]), [arch32 ? 'wineserver32' : 'wineserver64', '-f', '-p']);
  assert.equal(h.state.sessionPinsReady, false);
  h.tick(); assert.equal(h.spawns.length, 1); // no pid has changed yet
  h.state.pid = 12; h.tick();
  assert.equal(h.spawns.length, 2);
  assert.ok(h.spawns[1][1].endsWith(arch32 ? 'deskpin32.exe' : 'deskpin.exe'));
  assert.equal(h.state.sessionPinsReady, false);
  h.state.pid = 14; h.tick();
  assert.equal(h.state.sessionPinsReady, true);
  assert.equal(h.errors.length, 0);
});
test('a failed or undrained session pin never reports readiness', () => {
  for (const options of [{ spawnSuccess: false }, {}, { nativeReady: false }]) {
    const h = pinHarness(true, options); h.exhaust();
    assert.equal(h.state.sessionPinsReady, false);
    assert.ok(h.errors.length);
  }
});
test('no run is ready before the native kernel session is initialized', () => {
  for (const session of [false, true]) for (const done of [false, true]) {
    assert.equal(ready(session, done, false), false);
  }
});
