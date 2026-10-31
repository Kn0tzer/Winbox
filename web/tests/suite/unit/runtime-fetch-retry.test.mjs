import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

// fetchBytes downloads the rootfs zips. A single transient network failure
// must not discard the whole load: fetchBytes retries exactly once, loudly.
// A 404 is definitive (the caller falls back to chunked parts), so it must
// be rethrown immediately with no retry. These tests execute the launcher's
// actual fetchBytes function.
const source = readFileSync(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url), 'utf8');
const block = source.slice(source.indexOf('    function fetchBytes('),
  source.indexOf('    // The LIVE Emscripten module'));
assert.ok(block.includes('function fetchBytes'), 'fetchBytes must exist');
assert.ok(block.includes('BW64-FAIL fetchBytes'), 'fetchBytes failures must log a BW64-FAIL line');

function okResponse(bytes) {
  return {
    ok: true,
    status: 200,
    headers: { get: () => String(bytes.length) },
    arrayBuffer: async () => new Uint8Array(bytes).buffer,
  };
}

function harness(fetchImpl) {
  const warnings = [], errors = [];
  let calls = 0;
  const state = {
    fetch: (...args) => { calls++; return fetchImpl(...args); },
    fetchOpts: () => undefined,
    console: { log() {}, warn: t => warnings.push(t), error: t => errors.push(t) },
    setTimeout: (fn) => { fn(); return 1; },
  };
  runInNewContext(block + '; state => state', state);
  return { state, warnings, errors, calls: () => calls };
}

test('a transient failure is retried once and the download succeeds', async () => {
  let first = true;
  const h = harness(async () => {
    if (first) { first = false; throw new Error('network down'); }
    return okResponse([1, 2, 3]);
  });
  const bytes = await runInNewContext('fetchBytes("https://x/rootfs.zip", "rootfs.zip")', h.state);
  assert.deepEqual([...bytes], [1, 2, 3]);
  assert.equal(h.calls(), 2);
  assert.equal(h.warnings.length, 1);
  assert.match(h.warnings[0], /BW64-FAIL fetchBytes .*retrying once/);
  assert.equal(h.errors.length, 0);
});

test('a 404 is rethrown immediately with no retry and no failure line', async () => {
  const h = harness(async () => ({ ok: false, status: 404, headers: { get: () => null } }));
  await assert.rejects(
    runInNewContext('fetchBytes("https://x/rootfs.zip", "rootfs.zip")', h.state),
    /HTTP 404/);
  assert.equal(h.calls(), 1);
  assert.equal(h.warnings.length, 0);
  assert.equal(h.errors.length, 0);
});

test('a second failure gives up loudly and the error reaches the caller', async () => {
  const h = harness(async () => { throw new Error('network down'); });
  await assert.rejects(
    runInNewContext('fetchBytes("https://x/rootfs.zip", "rootfs.zip")', h.state),
    /network down/);
  assert.equal(h.calls(), 2);
  assert.equal(h.warnings.length, 1);
  assert.equal(h.errors.length, 1);
  assert.match(h.errors[0], /BW64-FAIL fetchBytes .*attempt=2.*giving up/);
});
