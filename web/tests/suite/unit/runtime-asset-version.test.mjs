import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
import { runInNewContext } from 'node:vm';

const source = readFileSync(join(repoRoot, 'project/emscripten/wine64-launcher.js'), 'utf8');
const start = source.indexOf('function runtimeAssetUrl(');
const end = source.indexOf('function bootWine(', start);
assert.ok(start >= 0 && end > start);
const assetUrl = version => runInNewContext(source.slice(start, end) + '; runtimeAssetUrl', {
  param: name => name === 'v' ? version : null,
});

test('main script and WASM use the same encoded release identifier', () => {
  const url = assetUrl('release&other=value');
  assert.equal(url('boxedwine64.js'), 'boxedwine64.js?v=release%26other%3Dvalue');
  assert.equal(url('boxedwine64.wasm', 'https://example.com/runtime/'),
    'https://example.com/runtime/boxedwine64.wasm?v=release%26other%3Dvalue');
  for (const file of ['boxedwine64.js', 'boxedwine64.wasm']) {
    assert.notEqual(assetUrl('old')(file), assetUrl('new')(file));
  }
});

test('direct runtime probes without a release keep their original asset URLs', () => {
  const url = assetUrl(null);
  assert.equal(url('boxedwine64.js'), 'boxedwine64.js');
  assert.equal(url('boxedwine64.wasm', '/runtime/'), '/runtime/boxedwine64.wasm');
});
