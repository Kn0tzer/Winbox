import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

// Execute the launcher's actual predicate with its native/session boundary.
const source = readFileSync(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url), 'utf8');
const predicate = source.match(/function sessionReady\(\) \{[\s\S]*?\n    \}/)?.[0];
assert.ok(predicate, 'Runtime readiness predicate must exist');
function ready(session, winebootDone, nativeReady) {
  return runInNewContext(predicate + '; sessionReady()', {
    SESSION: session, arch32WinebootDone: winebootDone, sessionReadyRaw: () => nativeReady,
  });
}

test('a standalone 32-bit game is ready without a persistent-session wineboot helper', () => {
  assert.equal(ready(false, false, true), true);
});
test('a persistent 32-bit session waits for its wineboot event handshake', () => {
  assert.equal(ready(true, false, true), false);
  assert.equal(ready(true, true, true), true);
});
test('no run is ready before the native kernel session is initialized', () => {
  for (const session of [false, true]) for (const done of [false, true]) {
    assert.equal(ready(session, done, false), false);
  }
});
