// web/tests/suite/unit/spirvfix-r13.test.mjs — SPIR-V fix R13 regression test.
//
// R13 "uniform-block-to-storage" serves the bridge's
// VK_KHR_uniform_buffer_standard_layout advertisement (the layout bit in
// vkd3d's shader-model 6.0 gate). A std430-packed uniform block is
// byte-identical to WGSL's storage layout but inexpressible in WGSL's
// uniform layout (array strides / struct sizes round up to 16 there), so
// naga rejects it. R13 demotes exactly those blocks -- decorated layout
// storage-expressible but not uniform-expressible -- to a read-only
// StorageBuffer variable. Scalar-packed blocks (vec3 arrays at stride 12,
// MatrixStride 12) match NEITHER WGSL layout and must stay unflipped:
// they keep failing loudly, which is the honest outcome for
// scalarBlockLayout = VK_FALSE.
//
// Fixtures: hand-built modules in testdata/spirv/r13/ (.spvasm sources
// alongside), one per layout shape in the honesty matrix.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs';
import { applySpirvFix, parseSpirv, serializeSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../../web/vendor/naga/node.js';

const load = (name) => new Uint8Array(fs.readFileSync(new URL(`../../../../testdata/spirv/r13/${name}`, import.meta.url)));
const RULE = 'uniform-block-to-storage';
const wgsl = (bytes) => String(naga.translate({ from: 'spirv', to: 'wgsl', source: bytes }));

const FLIPS = ['ubo-f32-stride4.spv', 'ubo-vec2-stride8.spv', 'ubo-arr-then-scalar.spv', 'ubo-nested-tight.spv'];
for (const f of FLIPS) {
  test(`${f}: std430 uniform block demoted to read-only storage and converts`, () => {
    const r = applySpirvFix(load(f));
    assert.ok(r.applied.includes(RULE), `R13 must fire (log: ${r.log.join(' | ')})`);
    const mod = parseSpirv(r.bytes);
    const bound = mod.insns.find((i) => i.op === 71 && i.words[1] === 34); // DescriptorSet
    const varIns = mod.insns.find((i) => i.op === 59 && i.resultId === bound.words[0]);
    assert.equal(varIns.words[2], 12, 'variable storage class must be StorageBuffer (12)');
    assert.ok(
      mod.insns.some((i) => i.op === 71 && i.words[0] === varIns.resultId && i.words[1] === 24),
      'variable must carry NonWritable (24) so naga emits read-only storage');
    const out = wgsl(r.bytes);
    assert.match(out, /var<storage> /, 'demoted block must be read-only storage in WGSL');
    assert.doesNotMatch(out, /var<storage, read_write>/, 'demoted block must not become writable storage');
    assert.match(out, /@binding\(0\)/, 'binding point must survive the demotion');
    // Idempotence: a second pass must not fire again.
    assert.ok(!applySpirvFix(r.bytes).applied.includes(RULE), 'R13 must be idempotent');
  });
}

test('ubo-f32-stride4: WGSL storage array keeps the guest byte layout (stride 4)', () => {
  const out = wgsl(applySpirvFix(load('ubo-f32-stride4.spv')).bytes);
  assert.match(out, /array<f32, 4>/, 'array<f32, 4> in storage has element stride 4 by WGSL rules');
});

const CONTROLS = ['ubo-vec4-stride16.spv', 'ubo-mixed-std430.spv'];
for (const f of CONTROLS) {
  test(`${f}: uniform-expressible block is left alone`, () => {
    const r = applySpirvFix(load(f));
    assert.ok(!r.applied.includes(RULE), 'R13 must not fire on a WGSL-uniform-expressible block');
    assert.match(wgsl(r.bytes), /var<uniform> /, 'block stays a uniform in WGSL');
  });
}

test('ubo-vec3-stride12: scalar-packed block is NOT demoted and still fails loudly', () => {
  const r = applySpirvFix(load('ubo-vec3-stride12.spv'));
  assert.ok(!r.applied.includes(RULE), 'stride-12 vec3 arrays match no WGSL layout: no demotion');
  assert.throws(() => wgsl(r.bytes), 'the module must keep failing at naga (honest scalar boundary)');
});

test('ubo-mat3arr-stride12: MatrixStride 12 matches neither WGSL layout, not demoted', () => {
  const r = applySpirvFix(load('ubo-mat3arr-stride12.spv'));
  assert.ok(!r.applied.includes(RULE), 'no demotion for matrix-stride-12 blocks');
});

test('ubo-pointer-phi: pointer escaping the access-chain trace refuses the demotion', () => {
  const r = applySpirvFix(load('ubo-pointer-phi.spv'));
  assert.ok(!r.applied.includes(RULE), 'no demotion when a block pointer flows through OpPhi');
  assert.ok(r.log.some((l) => l.includes('R13') && l.includes('refused')), 'the refusal must be logged');
});

test('sabotage: a wrong ArrayStride decoration defeats the demotion (checker is load-bearing)', () => {
  const mod = parseSpirv(load('ubo-f32-stride4.spv'));
  const deco = mod.insns.find((i) => i.op === 71 && i.words[1] === 6); // ArrayStride
  assert.equal(deco.words[2], 4);
  deco.words[2] = 8; // neither WGSL layout computes stride 8 for array<f32>
  const r = applySpirvFix(serializeSpirv(mod));
  assert.ok(!r.applied.includes(RULE), 'R13 must not fire when the decorated layout matches no WGSL layout');
});
