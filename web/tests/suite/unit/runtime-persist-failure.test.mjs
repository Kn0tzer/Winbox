import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

// The launcher's 5-second save sync used to swallow every failure: a
// failed walk skipped the tick, a failed IndexedDB transaction just
// reset a flag, a failed open was caught into nothing — saves silently
// stopped persisting. reportPersistFailure is the reporting half (the
// tick itself is the retry). These tests execute the real function
// block from the launcher and pin its wiring.
const source = readFileSync(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url), 'utf8');
const block = source.slice(source.indexOf('    var persistFailureCount = 0;'),
  source.indexOf('    function persistTick('));
assert.ok(block.includes('function reportPersistFailure'), 'reportPersistFailure must exist');
assert.ok(block.includes('BW64-FAIL persist'), 'persist failures must log a BW64-FAIL line');

test('persist failures report the first and every 64th, with the count', () => {
  const errors = [];
  const state = { console: { error: t => errors.push(t), warn() {}, log() {} } };
  runInNewContext(block + '; state => state', state);
  for (let i = 0; i < 65; i++) runInNewContext('reportPersistFailure("write", new Error("quota"))', state);
  assert.equal(errors.length, 2);
  assert.match(errors[0], /BW64-FAIL persist area=write error=quota count=1; sync retries on the next tick/);
  assert.match(errors[1], /BW64-FAIL persist area=write error=quota count=64; sync retries on the next tick/);
});

test('every persist failure site reports through the helper', () => {
  for (const area of ['walk', 'write', 'open']) {
    assert.ok(source.includes(`reportPersistFailure("${area}"`), `persist ${area} failure must be reported`);
  }
});
