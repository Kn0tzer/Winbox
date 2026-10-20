import test from 'node:test';
import assert from 'node:assert/strict';
import { repackScaledVb, v2VertexFormat } from '../../../vk-protocol.mjs';

const nativeFormats = new Map([
  [37, 'unorm8x4'], [38, 'snorm8x4'], [41, 'uint8x4'], [42, 'sint8x4'],
  [16, 'unorm8x2'], [17, 'snorm8x2'], [20, 'uint8x2'], [21, 'sint8x2'],
  [77, 'unorm16x2'], [78, 'snorm16x2'], [81, 'uint16x2'], [82, 'sint16x2'],
  [91, 'unorm16x4'], [92, 'snorm16x4'], [95, 'uint16x4'], [96, 'sint16x4'],
  [98, 'uint32'], [99, 'sint32'],
  [101, 'uint32x2'], [102, 'sint32x2'], [104, 'uint32x3'], [105, 'sint32x3'],
  [107, 'uint32x4'], [108, 'sint32x4'],
  [44, 'unorm8x4'], [64, 'unorm10-10-10-2'],
]);

test('DXVK integer, normalized and packed vertex formats map to native WebGPU formats', () => {
  for (const [format, expected] of nativeFormats) assert.equal(v2VertexFormat(format), expected, `VkFormat ${format}`);
});

test('three-component 8-bit and 16-bit vertex formats repack to native 32-bit attributes', () => {
  const conversions = new Map([
    [18, 'float32x2'], [19, 'float32x2'], [23, 'float32x3'], [24, 'float32x3'], [25, 'float32x3'], [26, 'float32x3'],
    [27, 'uint32x3'], [28, 'sint32x3'],
    [39, 'float32x4'], [40, 'float32x4'], [79, 'float32x2'], [80, 'float32x2'],
    [84, 'float32x3'], [85, 'float32x3'], [86, 'float32x3'], [87, 'float32x3'],
    [88, 'uint32x3'], [89, 'sint32x3'], [90, 'float32x3'],
    [93, 'float32x4'], [94, 'float32x4'],
  ]);
  for (const [format, expected] of conversions) assert.equal(v2VertexFormat(format), expected, `VkFormat ${format}`);
});

test('three-component normalized and integer values retain Vulkan input semantics when repacked', () => {
  const normalized = Uint8Array.from([0x80, 0x00, 0x7f]);
  const normOut = repackScaledVb(normalized, 3, 16, [{srcOffset:0,dstOffset:4,components:3,bits:8,signed:true,normalized:true}]);
  const normView = new DataView(normOut.buffer);
  assert.deepEqual([0,1,2].map(i => normView.getFloat32(4 + i * 4, true)), [-1, 0, 1]);

  const integer = new Uint8Array(6), integerView = new DataView(integer.buffer);
  integerView.setUint16(0, 1, true); integerView.setUint16(2, 65535, true); integerView.setUint16(4, 42, true);
  const intOut = repackScaledVb(integer, 6, 20, [{srcOffset:0,dstOffset:8,components:3,bits:16,signed:false,output:'uint32'}]);
  const intView = new DataView(intOut.buffer);
  assert.deepEqual([0,1,2].map(i => intView.getUint32(8 + i * 4, true)), [1, 65535, 42]);

  const half = new Uint8Array(6), halfView = new DataView(half.buffer);
  [0x3c00, 0xc000, 0x3800].forEach((bits, i) => halfView.setUint16(i * 2, bits, true));
  const halfOut = repackScaledVb(half, 6, 20, [{srcOffset:0,dstOffset:8,components:3,bits:16,signed:true,half:true}]);
  const floatView = new DataView(halfOut.buffer);
  assert.deepEqual([0,1,2].map(i => floatView.getFloat32(8 + i * 4, true)), [1, -2, 0.5]);
});
