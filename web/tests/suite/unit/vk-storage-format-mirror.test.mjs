// The bridge's typed-storage-read servable predicate (vk64bridge.cpp
// v2StorageLoadServable) and the page's format table (vk-protocol.mjs
// V2_STORAGE_FORMAT) are two hand-maintained copies of one contract: the
// bridge grants STORAGE_IMAGE + STORAGE_READ_WITHOUT_FORMAT for exactly
// these formats (which vkd3d turns into D3D12 UAV_TYPED_LOAD and hence the
// TypedUAVLoadAdditionalFormats device flag), and the page must be able to
// materialise each one as a WebGPU storage texture of the same format and
// hand spirvfix R14 the right SPIR-V ImageFormat value for it. This test
// pins the two lists to exact set equality, resolving the C++ enum names
// through vulkan_core.h so no number is trusted from either side, and
// checks the per-format wgsl/spv/kind triples agree with each other and
// with v2TextureFormat's materialisation.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { V2_STORAGE_FORMAT, v2TextureFormat } from '../../../vk-protocol.mjs';

const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');
const header = readFileSync(new URL('source/vulkan/vk/vulkan_core.h', root), 'utf8');

const ALL_FEATURES = new Set(['texture-compression-bc', 'depth32float-stencil8', 'float32-filterable', 'texture-formats-tier1', 'norm16', 'clip-distances', 'readonly-and-readwrite-storage-textures']);

// The 18 formats of vkd3d's d3d12_device_determine_additional_typed_uav_support
// set (as VkFormat numbers): TypedUAVLoadAdditionalFormats is TRUE only if
// every one of them earns UAV_TYPED_LOAD, so every one must be servable.
const D3D12_ADDITIONAL_FORMATS = [100, 98, 99, 109, 107, 108, 97, 95, 96, 37, 41, 42, 76, 74, 75, 9, 13, 14];

test('bridge v2StorageLoadServable set == page V2_STORAGE_FORMAT set', () => {
  const numByName = new Map();
  for (const m of header.matchAll(/VK_FORMAT_(\w+) = (\d+),/g)) numByName.set(m[1], Number(m[2]));
  const at = bridge.indexOf('static bool v2StorageLoadServable');
  assert.ok(at >= 0);
  const body = bridge.slice(at, bridge.indexOf('\n}\n', at));
  const bridgeSet = new Set();
  for (const m of body.matchAll(/case VK_FORMAT_(\w+):/g)) {
    assert.ok(numByName.has(m[1]), 'unknown format name ' + m[1]);
    bridgeSet.add(numByName.get(m[1]));
  }
  assert.equal(bridgeSet.size, 30, 'predicate should list the full servable set');
  const pageSet = new Set(Object.keys(V2_STORAGE_FORMAT).map(Number));
  assert.deepEqual([...bridgeSet].sort((a, b) => a - b), [...pageSet].sort((a, b) => a - b));
});

test('every servable format materialises under its storage wgsl name, kind-consistent', () => {
  for (const [vk, entry] of Object.entries(V2_STORAGE_FORMAT)) {
    assert.equal(v2TextureFormat(Number(vk), {}, ALL_FEATURES), entry.wgsl,
      `VkFormat ${vk} must materialise as ${entry.wgsl}`);
    const wgslKind = entry.wgsl.endsWith('uint') ? 'u' : entry.wgsl.endsWith('sint') ? 'i' : 'f';
    assert.equal(entry.kind, wgslKind, `VkFormat ${vk} kind must match its wgsl format kind`);
    assert.ok(Number.isInteger(entry.spv) && entry.spv > 0, `VkFormat ${vk} needs a concrete SPIR-V format`);
  }
});

test('all 18 D3D12 additional typed-UAV formats are servable', () => {
  for (const vk of D3D12_ADDITIONAL_FORMATS)
    assert.ok(V2_STORAGE_FORMAT[vk], `VkFormat ${vk} (D3D12 additional set) must be in V2_STORAGE_FORMAT`);
});
