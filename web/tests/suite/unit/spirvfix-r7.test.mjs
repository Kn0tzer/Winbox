// web/tests/suite/unit/spirvfix-r7.test.mjs — SPIR-V fix R7 regression test.
//
// REGRESSION TEST for the D3D12/vkd3d-proton naga translation failure
// (dxvk-d3d12-descset1 probe, 2026-10-08).
//
// R7: naga rejects OpCapability SignedZeroInfNanPreserve (4466) with
//     UnsupportedCapability. vkd3d-proton's vkd3d-shader emits it together
//     with OpExecutionMode SignedZeroInfNanPreserve (4461) for FP control.
//     Fix: strip the capability and any such execution-mode instructions.
//
// GENERALIZED 2026-10-26 (tropics-shader lane): naga rejects ALL FIVE
//     SPV_KHR_float_controls capabilities (4464-4468), and DXVK's DXBC
//     compiler emits DenormFlushToZero (4465) + OpExecutionMode 4460 on
//     real D3D11 shaders (captured from Unigine Tropics; see
//     testdata/spirv/dxvk-live/tropics-dxbc.vert.spv). R7 now strips the
//     whole family (caps 4464-4468, modes 4459-4463) under the rule name
//     'strip-float-controls'.
//
// This test constructs a minimal SPIR-V module exercising the case and
// verifies that applySpirvFix strips the pattern (byte-level regression
// guard). Full naga translation is verified by the in-browser D3D12 probe.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applySpirvFix, parseSpirv, serializeSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';

// Minimal SPIR-V module builder for testing specific rewrites
function buildModule(insns, bound = 100) {
  const header = { magic: 0x07230203, version: 0x00010600, generator: 0, bound, schema: 0 };
  return { header, insns };
}

// OpCapability instruction: op=17, words=[capability]
const cap = (c) => ({ op: 17, words: [c] });
// OpEntryPoint: op=15, words=[model, id, name..., interface...]
const entryPoint = (model, id) => ({ op: 15, words: [model, id, 0x6e69616d, 0] }); // "main\0"
// OpExecutionMode: op=16, words=[entryPointId, mode, ...params]
const execMode = (entry, mode) => ({ op: 16, words: [entry, mode] });
// OpExtension: op=10, words=[name...] (NUL-terminated, word-packed)
const extImport = (name) => {
  const bs = [...name].map(c => c.charCodeAt(0));
  bs.push(0);
  while (bs.length % 4) bs.push(0);
  const words = [];
  for (let k = 0; k < bs.length; k += 4)
    words.push((bs[k] | (bs[k + 1] << 8) | (bs[k + 2] << 16) | (bs[k + 3] << 24)) >>> 0);
  return { op: 10, words };
};

test('R7: SignedZeroInfNanPreserve capability and execution mode are stripped', () => {
  // Module declares the capability (4466) and uses the execution mode (4461),
  // matching what vkd3d-shader emits for D3D12 shaders.
  const mod = buildModule([
    cap(1),                          // Shader
    extImport('SPV_KHR_float_controls'),
    cap(4466),                       // SignedZeroInfNanPreserve
    entryPoint(0, 1),                // Vertex
    execMode(1, 4461),               // OpExecutionMode %1 SignedZeroInfNanPreserve
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    result.applied.includes('strip-float-controls'),
    'R7 should strip SignedZeroInfNanPreserve'
  );
  const outMod = parseSpirv(result.bytes);
  const hasCap = outMod.insns.some(i => i.op === 17 && i.words[0] === 4466);
  assert.ok(!hasCap, 'SignedZeroInfNanPreserve capability must be removed from output');
  const hasMode = outMod.insns.some(i => i.op === 16 && i.words[1] === 4461);
  assert.ok(!hasMode, 'SignedZeroInfNanPreserve execution mode must be removed from output');
  const hasExt = outMod.insns.some(i => i.op === 10);
  assert.ok(!hasExt, 'SPV_KHR_float_controls OpExtension must be removed from output');
});

test('R7: module without the pattern passes through untouched', () => {
  const mod = buildModule([
    cap(1),
    entryPoint(0, 1),
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    !result.applied.includes('strip-float-controls'),
    'R7 must not fire on a clean module'
  );
});

test('R7: DenormFlushToZero capability and execution mode are stripped (DXVK/Tropics pattern)', () => {
  // Module declares DenormFlushToZero (4465) and its execution mode with the
  // 32-bit width operand, exactly as DXVK's DXBC compiler emits it.
  const mod = buildModule([
    cap(1),                          // Shader
    cap(4465),                       // DenormFlushToZero
    entryPoint(0, 1),                // Vertex
    { op: 16, words: [1, 4460, 32] }, // OpExecutionMode %1 DenormFlushToZero 32
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    result.applied.includes('strip-float-controls'),
    'R7 should strip DenormFlushToZero'
  );
  const outMod = parseSpirv(result.bytes);
  assert.ok(!outMod.insns.some(i => i.op === 17 && i.words[0] === 4465),
    'DenormFlushToZero capability must be removed from output');
  assert.ok(!outMod.insns.some(i => i.op === 16 && i.words[1] === 4460),
    'DenormFlushToZero execution mode must be removed from output');
});

test('R7: rewrite is idempotent', () => {
  const mod = buildModule([
    cap(1),
    cap(4466),
    entryPoint(0, 1),
    execMode(1, 4461),
  ]);
  const once = applySpirvFix(serializeSpirv(mod));
  assert.ok(once.applied.includes('strip-float-controls'), 'first run applies R7');
  const twice = applySpirvFix(once.bytes);
  assert.ok(
    !twice.applied.includes('strip-float-controls'),
    'second run must be a passthrough for R7'
  );
});
