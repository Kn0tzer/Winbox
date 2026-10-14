import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

// Execute the launcher's actual predicate with its native/session boundary.
const source = readFileSync(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url), 'utf8');
const predicate = source.match(/function sessionReady\(\) \{[\s\S]*?\n    \}/)?.[0];
assert.ok(predicate, 'Runtime readiness predicate must exist');
function ready(session, pinsReady, nativeReady) {
  return runInNewContext(predicate + '; sessionReady()', {
    SESSION: session, sessionPinsReady: pinsReady, sessionReadyRaw: () => nativeReady,
  });
}

test('a standalone 32-bit game is ready without a persistent-session wineboot helper', () => {
  assert.equal(ready(false, false, true), true);
});
test('a persistent session waits for its server and desktop pin spawns', () => {
  assert.equal(ready(true, false, true), false);
  assert.equal(ready(true, true, true), true);
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
