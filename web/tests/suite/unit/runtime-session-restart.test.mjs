import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

// Session boot self-healing: when desktop initialization dies during a
// session boot (the measured unrecoverable-from-inside failure), the
// launcher navigates to the same URL for a clean boot, bounded by ?bwretry.
// These tests execute the launcher's actual watch/restart functions.
const source = readFileSync(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url), 'utf8');
const block = source.slice(source.indexOf('    // --- session boot self-healing'),
  source.indexOf('    function pidAlive'));
assert.ok(block.includes('function observeSessionHealth'), 'health observer must exist');
assert.ok(block.includes('function restartSessionBoot'), 'restart function must exist');
assert.equal(source.split('observeSessionHealth(text);').length - 1, 2,
  'both guest output streams (print and printErr) must feed the health observer');

const DOOM = '[guest fd=2 pid=14 wine64] 002c:err:winediag:nodrv_CreateWindow L"The explorer process failed to start."';
const HREF = 'http://127.0.0.1/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0';

function harness({ session = true, ready = false, bwretry = null } = {}) {
  const timers = [], navigations = [], errors = [], logs = [];
  const state = {
    SESSION: session,
    URL,
    sessionReady: () => state._ready,
    param: key => (key === 'bwretry' ? bwretry : null),
    console: { log: t => logs.push(t), warn() {}, error: t => errors.push(t) },
    setTimeout: (fn, ms) => { timers.push({ fn, ms }); return timers.length; },
    window: { location: { href: HREF, replace: url => navigations.push(url) } },
    _ready: ready,
  };
  runInNewContext(block + '; state => state', state);
  return { state, timers, navigations, errors, logs,
    fire() { assert.equal(timers.length, 1); timers.shift().fn(); } };
}

test('the desktop-init death signal schedules exactly one bounded restart', () => {
  const h = harness();
  runInNewContext(`observeSessionHealth(${JSON.stringify(DOOM)})`, h.state);
  assert.equal(h.timers.length, 1);
  assert.equal(h.timers[0].ms, 20000);
  runInNewContext(`observeSessionHealth(${JSON.stringify(DOOM)})`, h.state);
  assert.equal(h.timers.length, 1, 'a second print of the signal must not schedule again');
  assert.ok(h.errors.some(t => /desktop initialization failed/.test(t)));
});

test('unrelated output never schedules a restart', () => {
  const h = harness();
  runInNewContext('observeSessionHealth("deskpin: READY hwnd=0x10048")', h.state);
  runInNewContext('observeSessionHealth("session: persistent pins spawned")', h.state);
  assert.equal(h.timers.length, 0);
});

test('no restart watch outside a persistent session, or once ready', () => {
  for (const opts of [{ session: false }, { ready: true }]) {
    const h = harness(opts);
    runInNewContext(`observeSessionHealth(${JSON.stringify(DOOM)})`, h.state);
    assert.equal(h.timers.length, 0);
  }
});

test('the restart navigates to the same boot URL with the attempt incremented', () => {
  const h = harness();
  runInNewContext(`observeSessionHealth(${JSON.stringify(DOOM)})`, h.state);
  h.fire();
  assert.equal(h.navigations.length, 1);
  const url = new URL(h.navigations[0]);
  assert.equal(url.searchParams.get('bwretry'), '1');
  assert.equal(url.searchParams.get('session'), '1');
  assert.equal(url.searchParams.get('p'), 'cmd /c echo vkboot');
});

test('a session that becomes ready during the confirm window is left alone', () => {
  const h = harness();
  runInNewContext(`observeSessionHealth(${JSON.stringify(DOOM)})`, h.state);
  h.state._ready = true;
  h.fire();
  assert.equal(h.navigations.length, 0);
});

test('restarts are bounded: the final attempt does not navigate again', () => {
  const h = harness({ bwretry: '1' });
  runInNewContext(`observeSessionHealth(${JSON.stringify(DOOM)})`, h.state);
  h.fire();
  assert.equal(new URL(h.navigations[0]).searchParams.get('bwretry'), '2');
  const last = harness({ bwretry: '2' });
  runInNewContext(`observeSessionHealth(${JSON.stringify(DOOM)})`, last.state);
  last.fire();
  assert.equal(last.navigations.length, 0);
  assert.ok(last.errors.some(t => /final boot attempt/.test(t)));
});

test('the shared scheduler schedules one restart with the caller reason (FD-266)', () => {
  const h = harness();
  runInNewContext('scheduleSessionBootRestart("prehandshake fault loop")', h.state);
  assert.equal(h.timers.length, 1);
  assert.equal(h.timers[0].ms, 20000);
  assert.ok(h.errors.some(t => /prehandshake fault loop/.test(t)));
  runInNewContext('scheduleSessionBootRestart("prehandshake fault loop")', h.state);
  assert.equal(h.timers.length, 1, 'a second request must not schedule again');
});

test('the shared scheduler honors the session and readiness guards (FD-266)', () => {
  for (const opts of [{ session: false }, { ready: true }]) {
    const h = harness(opts);
    runInNewContext('scheduleSessionBootRestart("prehandshake fault loop")', h.state);
    assert.equal(h.timers.length, 0);
  }
});
