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
    if (name === 'fillCoreFeatures' && /textureCompressionBC\s*=\s*v2WebGpuBcSupported\(\)/.test(body))
      out.add('fillCoreFeatures.textureCompressionBC');
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

test('every Vulkan 1.0 core feature bit is explicitly implemented or not advertised', () => {
  const header = read('source/vulkan/vk64_guest.h');
  const core = header.match(/typedef struct VkPhysicalDeviceFeatures \{([\s\S]*?)\} VkPhysicalDeviceFeatures;/)?.[1];
  assert.ok(core, 'VkPhysicalDeviceFeatures ABI definition exists');
  const members = [...core.matchAll(/VkBool32\s+(\w+)\s*;/g)].map((m) => m[1]);
  const classified = new Set([
    ...Object.keys(manifest.features),
    ...manifest.notAdvertised,
  ].filter((name) => name.startsWith('fillCoreFeatures.')).map((name) => name.slice('fillCoreFeatures.'.length)));
  assert.deepEqual(members.filter((name) => !classified.has(name)), [], 'unclassified VkPhysicalDeviceFeatures members');
});

test('every advertised extension has a manifest row and every row is advertised', () => {
  for (const [array, key] of [['g_inst_exts', 'instance'], ['g_dev_exts', 'device']]) {
    const adv = advertisedExtensions(array);
    const rows = new Set(Object.keys(manifest.extensions[key]));
    assert.deepEqual([...adv].filter((k) => !rows.has(k)).sort(), [], `${array}: advertised without a row`);
    assert.deepEqual([...rows].filter((k) => !adv.has(k)).sort(), [], `${array}: row without advertisement`);
  }
});

test('known unsupported Vulkan extensions stay out of device extension enumeration', () => {
  const deviceExtensions = advertisedExtensions('g_dev_exts');
  for (const name of manifest.notAdvertisedExtensions) {
    assert.ok(!deviceExtensions.has(name), `${name} must remain absent until implemented`);
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

test('occlusion queries remain disabled because render sample counting is unavailable', () => {
  assert.ok(manifest.notAdvertised.includes('fillCoreFeatures.occlusionQueryPrecise'));
  assert.ok(manifest.notAdvertised.includes('fillCoreFeatures.occlusionQuery'));
  assert.doesNotMatch(bridge, /f->occlusionQueryPrecise\s*=\s*VK_TRUE/);
  assert.doesNotMatch(bridge, /f->occlusionQuery\s*=\s*VK_TRUE/);
  assert.match(bridge, /"occlusion queries are not implemented; device feature bit is false"/);
});

test('descriptor-set limit matches the bridge record capacity; shader groups stay tier-gated', () => {
  // Since the 2026-10-26 vkd3d disposition the advertised limit is the
  // bridge's record capacity (VK64_MAX_SETS): vkd3d root signatures require
  // six sets at layout CREATION. What shaders may use stays gated by the
  // page tier at pipeline build (groups 0-2 + push; "group-out-of-range").
  assert.match(bridge, /L\.maxBoundDescriptorSets = VK64_MAX_SETS/);
  const marker = 'case VK64_fn_vkCreatePipelineLayout: {';
  const first = bridge.indexOf(marker), second = bridge.indexOf(marker, first + marker.length);
  assert.ok(first >= 0 && second > first, 'both guest ABIs have a pipeline-layout handler');
  const layouts = [functionBody(bridge.slice(first), marker), functionBody(bridge.slice(second), marker)];
  for (const body of layouts) {
    assert.match(body, /ci\.setLayoutCount > VK64_MAX_SETS/);
    assert.match(body, /vk64Unsupported\("descriptor-set-count"/);
    assert.match(body, /return VK_ERROR_FEATURE_NOT_PRESENT/);
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

test('Features2 output zeroes unsupported guest input bits before setting implemented bits', () => {
  const set64Start = bridge.indexOf('#define SET_FEATURES(addr, fn, T)');
  const set64 = bridge.slice(set64Start, bridge.indexOf('// Every TRUE bit', set64Start));
  const set32Start = bridge.indexOf('#define SET_FEATURES32(addr, fn, T)');
  const set32 = bridge.slice(set32Start, bridge.indexOf('// 32-bit field writers', set32Start));
  assert.match(set64, /memcpyFromGuest\(&f_, \(addr\), 16\)/);
  assert.doesNotMatch(set64, /memcpyFromGuest\([^\n]*sizeof\(f_\)/);
  assert.match(set32, /memcpyFromGuest\(&f_, \(addr\), 8\)/);
  assert.doesNotMatch(set32, /readStruct32\(\(addr\), f_\)/);
});
