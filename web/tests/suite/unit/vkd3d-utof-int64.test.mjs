// Vendored dxbc-spirv lowers HLSL utof (u32 -> f32 conversion) in
// ArithmeticPass::lowerConvertItoF. Upstream widens the U32 input to U64
// first, as a workaround for a broken Nvidia driver (580.76.05 treats
// UtoF inputs as signed). On this stack that widening is fatal: it puts
// OpCapability Int64 (and a 64-bit integer type) into every module that
// contains a u32-to-float conversion, and vkd3d-proton's
// d3d12_device_validate_shader_meta then rejects the shader with
// E_INVALIDARG at CreateGraphicsPipelineState because the WebGPU ICD
// honestly reports Int64ShaderOps == false (uav12's R32_UINT / R8_UINT
// typed-UAV cases were the probes that pinned this). The vendored patch
// emits the direct conversion instead: OpConvertUToF on a U32 input is
// spec-correct (round-to-nearest-even, identical to D3D utof semantics)
// on the only driver this converter ever targets. These tests pin the
// patch so a re-vendor or a well-meaning cleanup cannot silently
// restore the widening; they read the vendored source directly, in the
// style of vk-storage-format-mirror.test.mjs.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const arith = readFileSync(new URL(
  'tools/vkd3d/src/subprojects/dxil-spirv/subprojects/dxbc-spirv/ir/passes/ir_pass_arithmetic.cpp',
  root), 'utf8');

function lowerConvertItoFBody() {
  const at = arith.indexOf('ArithmeticPass::lowerConvertItoF');
  assert.ok(at >= 0, 'lowerConvertItoF not found in vendored ir_pass_arithmetic.cpp');
  const next = arith.indexOf('ArithmeticPass::', at + 10);
  return arith.slice(at, next > at ? next : at + 4000);
}

test('utof lowering carries the Boxedwine64 patch marker', () => {
  assert.match(lowerConvertItoFBody(), /Boxedwine64 patch/);
});

test('utof lowering does not widen U32 to U64 (no Int64 injection)', () => {
  const body = lowerConvertItoFBody();
  assert.ok(!/eU64/.test(body),
    'lowerConvertItoF references eU64: the Int64-injecting widening is back');
  assert.ok(!/ConvertItoI\(ScalarType::eU64/.test(body));
});

test('utof lowering still widens U16 to U32 (32-bit-contained, harmless)', () => {
  const body = lowerConvertItoFBody();
  assert.match(body, /srcType == ScalarType::eU16/);
  assert.match(body, /ConvertItoI\(ScalarType::eU32, v\)/);
});

test('utof lowering still emits the direct ConvertItoF on the (possibly widened-to-32) value', () => {
  assert.match(lowerConvertItoFBody(), /Op::ConvertItoF\(dstType, v\)/);
});
