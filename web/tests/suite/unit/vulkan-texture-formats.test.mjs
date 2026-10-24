import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import {
  v2DepthStencilUploads, v2TextureFormat, v2TexturePixels, v2VertexFormat,
} from '../../../vk-protocol.mjs';
import {
  V2_COLOR_ATTACHMENT_BLEND_FORMATS, V2_COLOR_ATTACHMENT_FORMATS,
  V2_DEPTH_STENCIL_FORMATS, V2_SUPPORTED_TEXTURE_FORMATS, V2_SUPPORTED_VERTEX_FORMATS,
} from '../../../vkwebgpu.mjs';

test('requested Vulkan texture formats map to WebGPU formats', () => {
  const formats = new Map([
    [76, 'r16float'], [83, 'rg16float'], [103, 'rg32float'],
    [98, 'r32uint'], [99, 'r32sint'], [124, 'depth16unorm'],
    [125, 'depth32float'], [126, 'depth32float'], [129, 'depth24plus-stencil8'],
  ]);
  for (const [vkFormat, expected] of formats)
    assert.equal(v2TextureFormat(vkFormat), expected, `VkFormat ${vkFormat}`);
});

test('R16 UNORM and R16G16 UNORM use core float formats without optional norm16 support', () => {
  assert.equal(v2TextureFormat(70), 'r16float');
  assert.equal(v2TextureFormat(77), 'rg16float');
  assert.equal(v2TextureFormat(70, null, new Set(['texture-formats-tier1'])), 'r16unorm');
  assert.equal(v2TextureFormat(77, null, new Set(['norm16'])), 'rg16unorm');
  const raw = new Uint8Array([0, 0, 0, 128, 255, 255]);
  const converted = v2TexturePixels(70, raw);
  const view = new DataView(converted.buffer);
  assert.deepEqual([0, 1, 2].map(i => view.getUint16(i * 2, true)), [0, 0x3800, 0x3c00]);
  assert.deepEqual([...v2TexturePixels(77, raw)], [...converted]);
  assert.deepEqual([...v2TexturePixels(70, raw, new Set(['texture-formats-tier1']))], [...raw]);
});

test('D32 float-stencil uses depth24plus-stencil8 unless its optional format feature is enabled', () => {
  assert.equal(v2TextureFormat(130), 'depth24plus-stencil8');
  assert.equal(v2TextureFormat(130, null, new Set(['depth32float-stencil8'])), 'depth32float-stencil8');
  assert.equal(v2TextureFormat(129, null, new Set(['depth32float-stencil8'])), 'depth32float-stencil8');
});

test('depth and stencil uploads use separate WebGPU aspects and preserve depth/stencil values', () => {
  const d16 = Uint8Array.from([1, 2, 3, 4]);
  assert.deepEqual(v2DepthStencilUploads(124, d16).uploads.map(x => [x.aspect, [...x.pixels]]), [['depth-only', [1, 2, 3, 4]]]);
  const d24s8 = Uint8Array.from([1, 2, 3, 0xa5, 4, 5, 6, 0x5a]);
  const fallback = v2DepthStencilUploads(129, d24s8);
  assert.equal(fallback.skippedDepth, true);
  assert.deepEqual(fallback.uploads.map(x => [x.aspect, x.bytesPerTexel, [...x.pixels]]), [['stencil-only', 1, [0xa5, 0x5a]]]);
  const d24s8Direct = v2DepthStencilUploads(129, d24s8, new Set(['depth32float-stencil8']));
  assert.equal(d24s8Direct.skippedDepth, false);
  assert.deepEqual(d24s8Direct.uploads.map(x => x.aspect), ['depth-only', 'stencil-only']);
  const d24s8Depth = new DataView(d24s8Direct.uploads[0].pixels.buffer);
  assert.ok(Math.abs(d24s8Depth.getFloat32(0, true) - 0x030201 / 0xffffff) < 1e-7);
  assert.deepEqual([...d24s8Direct.uploads[1].pixels], [0xa5, 0x5a]);
  const d32s8 = Uint8Array.from([1, 2, 3, 4, 0x81, 0, 0, 0]);
  const direct = v2DepthStencilUploads(130, d32s8, new Set(['depth32float-stencil8']));
  assert.equal(direct.skippedDepth, false);
  assert.deepEqual(direct.uploads.map(x => [x.aspect, x.bytesPerTexel, [...x.pixels]]), [
    ['depth-only', 4, [1, 2, 3, 4]], ['stencil-only', 1, [0x81]],
  ]);
  const d24 = v2DepthStencilUploads(125, Uint8Array.from([0, 0, 0x80, 0, 0xff, 0xff, 0xff, 0]));
  assert.equal(d24.skippedDepth, false);
  assert.deepEqual(d24.uploads.map(x => [x.aspect, x.bytesPerTexel]), [['depth-only', 4]]);
  const expandedDepth = new DataView(d24.uploads[0].pixels.buffer);
  assert.ok(Math.abs(expandedDepth.getFloat32(0, true) - 0x800000 / 0xffffff) < 1e-7);
  assert.equal(expandedDepth.getFloat32(4, true), 1);
});

test('legacy packed 16-bit color formats expand to RGBA8 in Vulkan component order', () => {
  const packed = (format, word) => v2TexturePixels(format, Uint8Array.from([word & 255, word >> 8]));
  assert.deepEqual([...packed(2, 0xf00f)], [255, 0, 0, 255]); // R4G4B4A4
  assert.deepEqual([...packed(3, 0xf00f)], [0, 0, 255, 255]); // B4G4R4A4
  assert.deepEqual([...packed(5, 0xf800)], [0, 0, 255, 255]); // B5G6R5
  assert.deepEqual([...packed(7, 0xf801)], [0, 0, 255, 255]); // B5G5R5A1
  assert.deepEqual([...packed(8, 0x8000)], [0, 0, 0, 255]); // A1R5G5B5
});

test('unmapped formats are counted and do not receive a fallback texture format', () => {
  const counters = {};
  assert.equal(v2TextureFormat(0x7fffffff, counters), null);
  assert.equal(counters.unknownFormats, 1);
});

test('BC formats are supported and mapped only when texture-compression-bc is enabled', () => {
  assert.equal(v2TextureFormat(131), null);
  assert.equal(v2TextureFormat(131, null, new Set(['texture-compression-bc'])), 'bc1-rgba-unorm');
  assert.ok(V2_SUPPORTED_TEXTURE_FORMATS.some(format => format >= 131 && format <= 146));
  for (let format = 131; format <= 146; format++)
    assert.notEqual(v2TextureFormat(format, null, new Set(['texture-compression-bc'])), null, `VkFormat ${format}`);
});

test('bridge format-support tables match the page tables', async () => {
  const cpp = await readFile(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const extract = name => {
    const match = cpp.match(new RegExp(`static const U32 ${name}\\[\\] = \\{([^}]+)\\}`));
    assert.ok(match, `${name} is present in vk64bridge.cpp`);
    return [...match[1].matchAll(/\d+/g)].map(([value]) => Number(value));
  };
  assert.deepEqual(extract('kV2SupportedTextureFormats'), [...V2_SUPPORTED_TEXTURE_FORMATS]);
  assert.deepEqual(extract('kV2SupportedVertexFormats'), [...V2_SUPPORTED_VERTEX_FORMATS]);
  assert.deepEqual(extract('kV2ColorAttachmentFormats'), [...V2_COLOR_ATTACHMENT_FORMATS]);
  assert.deepEqual(extract('kV2ColorAttachmentBlendFormats'), [...V2_COLOR_ATTACHMENT_BLEND_FORMATS]);
  assert.deepEqual(extract('kV2DepthStencilFormats'), [...V2_DEPTH_STENCIL_FORMATS]);
  assert.ok(V2_COLOR_ATTACHMENT_BLEND_FORMATS.every(format => V2_COLOR_ATTACHMENT_FORMATS.includes(format)));
  for (const format of V2_SUPPORTED_TEXTURE_FORMATS)
    assert.notEqual(v2TextureFormat(format, null, new Set(['texture-compression-bc'])), null, `texture VkFormat ${format}`);
  for (const format of V2_SUPPORTED_VERTEX_FORMATS)
    assert.ok(v2VertexFormat(format), `vertex VkFormat ${format}`);
  assert.match(cpp, /case VK64_fn_vkGetPhysicalDeviceFormatProperties:[\s\S]*?v2FormatFeatureFlags\(format\)/);
  assert.match(cpp, /case VK64_fn_vkGetPhysicalDeviceFormatProperties2:[\s\S]*?v2FormatFeatureFlags\(format\)/);
  assert.match(cpp, /if \(v2FormatIsBlendable\(format\)\) flags \|= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT/);
  assert.match(cpp, /static VkFormatFeatureFlags2 v2FormatFeatureFlags2[\s\S]*?if \(v2FormatIsBlendable\(format\)\) flags \|= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT/);
});

test('D24S8 and D32S8 transfer destinations are advertised only when uploads are representable', async () => {
  const cpp = await readFile(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  assert.match(cpp, /v2FormatTransferDstSupported\(U32 format\)[\s\S]*?v2WebGpuDepth32FloatStencil8Supported\(\)/);
  assert.match(cpp, /v2FormatFeatureFlags\(U32 format\)[\s\S]*?if \(v2FormatTransferDstSupported\(format\)\)[\s\S]*?VK_FORMAT_FEATURE_TRANSFER_DST_BIT/);
  assert.match(cpp, /v2FormatFeatureFlags2\(U32 format\)[\s\S]*?if \(v2FormatTransferDstSupported\(format\)\)[\s\S]*?VK_FORMAT_FEATURE_TRANSFER_DST_BIT/);
  assert.equal((cpp.match(/vk64Unsupported\("depth-stencil-transfer-dst"/g) || []).length, 2,
    'both 64-bit and 32-bit image creation reject unsupported transfer-destination use');
  assert.equal((cpp.match(/VK_ERROR_FORMAT_NOT_SUPPORTED;/g) || []).length >= 2, true);
});
