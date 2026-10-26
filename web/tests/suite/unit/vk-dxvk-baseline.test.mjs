// GUARD (DXVK baseline vs bridge advertisement): DXVK's D3D11 device creation
// is all-or-nothing. D3D11Device::GetMaxFeatureLevel returns 0 unless every
// feature that D3D11Device::GetDeviceFeatures hard-requires (literal VK_TRUE)
// is also reported supported by the Vulkan ICD -- here, the bridge's feature
// fill functions (whose advertised set vk-capabilities.test.mjs pins to
// source/vulkan/vk-capabilities.json). The 2026-10-23 "truthful capability
// manifest" change cut the bridge's advertisements below DXVK's baseline and
// silently killed ALL DXVK D3D11/D3D10 device creation on both guest
// architectures; it was first observed on 2026-10-26 as the 32-bit Tropics
// benchmark logging "Maximum supported feature level: 0". The fix mirrors the
// unsatisfiable requirements from the adapter's reported support inside the
// vendored DXVK (tools/dxvk/src/src/d3d11/d3d11_device.cpp). This test pins
// the contract from both sides so neither side can drift back into the
// breakage: DXVK must never hard-require a feature the bridge does not
// advertise, and the features DXVK was patched to mirror must stay mirrored.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const read = (p) => readFileSync(new URL(p, root), 'utf8');
const stripComments = (s) => s.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

const dxvk = stripComments(read('tools/dxvk/src/src/d3d11/d3d11_device.cpp'));
const manifest = JSON.parse(read('source/vulkan/vk-capabilities.json'));

// The body of D3D11Device::GetDeviceFeatures: every `enabled.<path> = VK_TRUE`
// in it is a feature DXVK refuses to create a device without.
function getDeviceFeaturesBody() {
  const at = dxvk.indexOf('DxvkDeviceFeatures D3D11Device::GetDeviceFeatures');
  assert.ok(at >= 0, 'vendored DXVK GetDeviceFeatures exists');
  const end = dxvk.indexOf('return enabled;', at);
  assert.ok(end > at, 'GetDeviceFeatures body terminates');
  return dxvk.slice(at, end);
}

// DXVK feature-struct path -> the bridge fill function whose manifest rows
// (fillFn.field) describe the same Vulkan feature struct.
const structToFill = {
  'core.features': 'fillCoreFeatures',
  vk11: 'fillVulkan11Features',
  vk12: 'fillVulkan12Features',
  vk13: 'fillVulkan13Features',
  extCustomBorderColor: 'fillCustomBorderColor',
  extDepthClipEnable: 'fillDepthClipEnable',
  extFragmentShaderInterlock: 'fillFragmentShaderInterlock',
  extRobustness2: 'fillRobustness2',
  extTransformFeedback: 'fillTransformFeedback',
  extVertexAttributeDivisor: 'fillVertexAttributeDivisor',
};

function manifestKey(dxvkPath) {
  const cut = dxvkPath.lastIndexOf('.');
  const fill = structToFill[dxvkPath.slice(0, cut)];
  assert.ok(fill, `no bridge fill mapping for DXVK feature path ${dxvkPath} -- extend structToFill`);
  return `${fill}.${dxvkPath.slice(cut + 1)}`;
}

function assignments(body, valuePattern) {
  const out = new Set();
  for (const m of body.matchAll(new RegExp(`enabled\\.((?:core\\.features|\\w+)\\.\\w+)\\s*=\\s*${valuePattern}`, 'g')))
    out.add(m[1]);
  return out;
}

test('DXVK hard-required features never exceed bridge advertisements', () => {
  const hard = assignments(getDeviceFeaturesBody(), 'VK_TRUE');
  const advertised = new Set(Object.keys(manifest.features));
  assert.ok(hard.size >= 9, `parser sanity: expected at least 9 hard requirements, saw ${hard.size}`);
  const uncovered = [...hard].map(manifestKey).filter((k) => !advertised.has(k)).sort();
  assert.deepEqual(uncovered, [], 'DXVK hard-requires features the bridge does not advertise (device creation returns feature level 0)');
});

test('features the WebGPU ICD cannot implement stay mirrored from reported support in DXVK', () => {
  const body = getDeviceFeaturesBody();
  const hard = assignments(body, 'VK_TRUE');
  const mirrored = assignments(body, String.raw`supported\.(?:core\.features|\w+)\.\w+;`);
  // The 2026-10-26 fix: these eleven baseline entries must be taken from the
  // adapter's reported support, never hard-required -- the bridge honestly
  // reports them unsupported (see vk-capabilities.json notAdvertised), and
  // hard-requiring any one of them zeroes every DXVK D3D11/D3D10 device.
  const mustMirror = [
    'core.features.dualSrcBlend',
    'core.features.fillModeNonSolid',
    'core.features.geometryShader',
    'core.features.multiViewport',
    'core.features.occlusionQueryPrecise',
    'core.features.sampleRateShading',
    'core.features.shaderCullDistance',
    'core.features.shaderImageGatherExtended',
    'vk12.samplerMirrorClampToEdge',
    'extTransformFeedback.transformFeedback',
    'extTransformFeedback.geometryStreams',
  ];
  for (const path of mustMirror) {
    assert.ok(!hard.has(path), `${path} must not be hard-required (VK_TRUE) in GetDeviceFeatures`);
    assert.ok(mirrored.has(path), `${path} must be mirrored from supported.* in GetDeviceFeatures`);
  }
});

test('page-flag-gated DXVK baseline features are published before guest boot', () => {
  // textureCompressionBC stays hard-required in DXVK (it is genuinely
  // servable), but the bridge fills it from a page-published flag:
  // vk64bridge.cpp v2WebGpuBcSupported() reads
  // globalThis.bwVkTextureCompressionBC. That flag is also written by the
  // renderer tier (web/vkwebgpu.mjs createVkRenderer*), which is only
  // constructed at first frame -- AFTER the guest queries device features
  // at D3D11 device creation. The runtime page must therefore publish the
  // adapter-derived flags itself, before the guest boots, or DXVK reads
  // BC as unsupported and every D3D11/D3D10 device fails with feature
  // level 0 (the second half of the 2026-10-26 defect).
  const runtime = read('web/runtime.html');
  assert.match(runtime, /bwVkTextureCompressionBC\s*=\s*adapter\.features\?\.has\('texture-compression-bc'\)/,
    'runtime.html must publish bwVkTextureCompressionBC from the adapter before guest boot');
  assert.match(runtime, /bwVkDepth32FloatStencil8\s*=\s*adapter\.features\?\.has\('depth32float-stencil8'\)/,
    'runtime.html must publish bwVkDepth32FloatStencil8 from the adapter before guest boot');
  const bridge = read('source/vulkan/vk64bridge.cpp');
  assert.match(bridge, /textureCompressionBC\s*=\s*v2WebGpuBcSupported\(\)/,
    'bridge BC fill must stay gated on v2WebGpuBcSupported (chain premise of this guard)');
});
