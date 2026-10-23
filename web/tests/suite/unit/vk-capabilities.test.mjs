// GUARD (advertised-but-unimplemented): every capability the bridge advertises (VkBool32 TRUE in a
// feature fill function, or an extension name in g_inst_exts/g_dev_exts) must have a row in
// source/vulkan/vk-capabilities.json. A row is either `implemented`, or `exception` carrying a
// `reject` token that must exist in the source where the unsupported use is reported loudly.
// Anything absent from the manifest is a silent lie to DXVK/vkd3d.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const read = (p) => readFileSync(new URL(p, root), 'utf8');
const bridge = read('source/vulkan/vk64bridge.cpp');
const manifest = JSON.parse(read('source/vulkan/vk-capabilities.json'));
const stripComments = (s) => s.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

function functionBody(src, header) {
  const at = src.indexOf(header);
  assert.ok(at >= 0, `missing ${header}`);
  const open = src.indexOf('{', at);
  let depth = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}' && --depth === 0) return src.slice(open, i);
  }
  throw new Error('unbalanced ' + header);
}

function advertisedFeatures() {
  const out = new Set();
  const fills = [...bridge.matchAll(/inline void (fill\w+)\(T&\s*\w*\)\s*\{/g)].map((m) => m[1]);
  for (const name of ['fillCoreFeatures', ...fills]) {
    const hdr = name === 'fillCoreFeatures' ? 'void fillCoreFeatures(' : `inline void ${name}(T&`;
    const body = stripComments(functionBody(bridge, hdr));
    for (const m of body.matchAll(/\bf(?:->|\.)(\w+)\s*=\s*VK_TRUE/g)) out.add(`${name}.${m[1]}`);
  }
  return out;
}
function advertisedExtensions(array) {
  const body = stripComments(functionBody(bridge, `const char* const ${array}[]`));
  return new Set([...body.matchAll(/"(VK_\w+)"/g)].map((m) => m[1]));
}

test('every advertised feature bit has a manifest row and every row is advertised', () => {
  const adv = advertisedFeatures();
  const rows = new Set(Object.keys(manifest.features));
  assert.deepEqual([...adv].filter((k) => !rows.has(k)).sort(), [], 'advertised without a manifest row');
  assert.deepEqual([...rows].filter((k) => !adv.has(k)).sort(), [], 'manifest row for a bit that is not advertised');
});

test('every advertised extension has a manifest row and every row is advertised', () => {
  for (const [array, key] of [['g_inst_exts', 'instance'], ['g_dev_exts', 'device']]) {
    const adv = advertisedExtensions(array);
    const rows = new Set(Object.keys(manifest.extensions[key]));
    assert.deepEqual([...adv].filter((k) => !rows.has(k)).sort(), [], `${array}: advertised without a row`);
    assert.deepEqual([...rows].filter((k) => !adv.has(k)).sort(), [], `${array}: row without advertisement`);
  }
});

test('no advertised feature or extension may use an exception status', () => {
  for (const [name, row] of Object.entries(manifest.features)) {
    assert.equal(row.status, 'implemented', `${name} must be implemented before advertisement`);
  }
  for (const [scope, entries] of Object.entries(manifest.extensions)) {
    for (const [name, row] of Object.entries(entries)) {
      assert.equal(row.status, 'implemented', `${scope} ${name} must be implemented before advertisement`);
    }
  }
});

test('features that are not implemented are not advertised', () => {
  const adv = advertisedFeatures();
  for (const k of manifest.notAdvertised) assert.ok(!adv.has(k), `${k} must stay FALSE`);
});

test('every exception row names a loud rejection that exists in the source', () => {
  const sources = ['source/vulkan/vk64bridge.cpp', 'web/vkwebgpu.mjs', 'web/vk-protocol.mjs'].map(read).join('\n');
  const rows = [...Object.entries(manifest.features), ...Object.values(manifest.extensions).flatMap((e) => Object.entries(e))];
  for (const [name, row] of rows) {
    assert.ok(row.status === 'implemented' || row.status === 'exception', `${name}: bad status ${row.status}`);
    if (row.status === 'exception') {
      assert.ok(row.reject, `${name}: exception without a reject token`);
      assert.ok(sources.includes(row.reject), `${name}: reject token "${row.reject}" not found in source`);
    }
  }
});

test('32-bit and 64-bit Features2 walkers answer from the same fill functions', () => {
  const w64 = [...bridge.matchAll(/SET_FEATURES\(cur, (\w+),/g)].map((m) => m[1]).sort();
  const w32 = [...bridge.matchAll(/SET_FEATURES32\(cur, (\w+),/g)].map((m) => m[1]).sort();
  assert.ok(w64.length > 10);
  assert.deepEqual(w32, w64);
  assert.match(bridge, /fillCoreFeatures\(&f\);\s*\/\/ same answer as the 64-bit guest/);
});
