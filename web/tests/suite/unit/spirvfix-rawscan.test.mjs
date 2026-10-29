// web/tests/suite/unit/spirvfix-rawscan.test.mjs — rawscan audit guards.
//
// The defect class: SPIR-V analysis scans that compare RAW instruction
// words against a sought id, without distinguishing id-typed operands
// from literal operands (constant values, decoration parameters,
// component indexes, alignment literals). A literal that happens to
// equal a small id is then mistaken for a use of that id and the pass
// false-declines a servable module. The spirvfix veto scans and the
// bridge audit's root check now walk only id-typed operand positions
// (idOperandUseIndex in tools/spirvfix/spirvfix.mjs, mirrored in
// source/vulkan/vk64bridge.cpp). Each fixture below is a minimal
// module whose ONLY trigger for the pre-fix decline is one such
// literal; the pre-fix code mishandles every one of them (verified
// per fixture by reverting its call site — the sabotage check).
//
// Fixtures (testdata/spirv/rawscan/, hand-written .spvasm assembled
// with spirv-as --preserve-numeric-ids; each .spv is spirv-val clean
// under the environment noted in its .spvasm provenance):
//   drawparam-constant-collision.spv — vulkan1.1
//   texel-constant-collision.spv     — spv1.5 (sampled Buffer images
//                                      are a pre-1.6 shape)
//   uniform-shuffle-literal.spv      — spv1.3
//   layer-copymemory-literal.spv     — default env (the Vulkan Flat
//                                      VUID for integer inputs is what
//                                      real DXVK Layer modules violate;
//                                      R11 exists for that shape)
//   storage-texel-width-collision.spv — vulkan1.1 (R12S storage texel:
//                                      the image load is %32 and the
//                                      uint type's width literal is 32;
//                                      R12S itself arrives with
//                                      lane-metabind, imported verbatim
//                                      on this branch, its consumer
//                                      scan fixed here)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  applySpirvFix, parseSpirv, serializeSpirv, analyzeRootDescriptors,
} from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../../web/vendor/naga/node.js';

await naga.init();
const fx = (name) => new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/rawscan/' + name, import.meta.url)));
const wgsl = (bytes) => String(naga.translate({ from: 'spirv', to: 'wgsl', source: bytes }));

test('rawscan F1 (R5): an OpConstant value equal to the builtin variable id is not a pointer use', () => {
  // BaseVertex variable at id 10; %11 = OpConstant %int 10 feeds an
  // OpIAdd. The constant's value word was the only "use" of 10 outside
  // the load; the raw scan declined the module as "pointer is used
  // outside OpLoad".
  const r = applySpirvFix(fx('drawparam-constant-collision.spv'));
  assert.ok(r.applied.includes('replace-draw-parameters'), r.log.join('; '));
  assert.ok(!r.log.some(l => l.includes('used outside OpLoad')), r.log.join('; '));
  const mod = parseSpirv(r.bytes);
  assert.ok(!mod.insns.some(i => i.op === 17 && i.words[0] === 4427), 'DrawParameters capability must be gone');
  assert.ok(!mod.insns.some(i => i.resultId === 10), 'the builtin variable must be gone');
  // (No naga translate here: this minimal vertex shader writes no
  // gl_Position, which naga's entry-point validation requires -- the
  // r5-r6 suite covers translation of R5 output on full DXVK shaders.)
});

test('rawscan F2 (R12): an OpConstant value equal to the converted load id does not veto the texel conversion', () => {
  // usamplerBuffer texelFetch; the sampled-image load is id 40 and
  // the fetch coordinate is OpConstant 40. The raw veto scan read the
  // constant as "converted image value feeds opcode 43" and refused.
  const r = applySpirvFix(fx('texel-constant-collision.spv'));
  assert.ok(r.applied.includes('texel-buffer-to-2d'), r.log.join('; '));
  assert.ok(!r.log.some(l => l.includes('conversion refused')), r.log.join('; '));
  const out = wgsl(r.bytes);
  assert.match(out, /texture_2d<u32>/);
  assert.match(out, /textureLoad/);
});

test('rawscan F3 (R13): an OpVectorShuffle component literal equal to the block variable id does not abort the demotion', () => {
  // struct{float[4]} block (stride 4: storage-shaped, not uniform)
  // with the block variable at id 7; an OpVectorShuffle over untraced
  // values carries component literal 7. The raw trace read the
  // literal as the traced pointer escaping and refused the demotion.
  const r = applySpirvFix(fx('uniform-shuffle-literal.spv'));
  assert.ok(r.applied.includes('uniform-block-to-storage'), r.log.join('; '));
  const mod = parseSpirv(r.bytes);
  const v = mod.insns.find(i => i.resultId === 7);
  assert.ok(v && v.op === 59);
  assert.equal(v.words[2], 12, 'the variable must be re-rooted at StorageBuffer');
  wgsl(r.bytes);
});

test('rawscan F4 (R11): an OpCopyMemory Aligned literal equal to the Layer variable id is not a pointer use', () => {
  // Layer input variable at id 16, only ever loaded; an unrelated
  // OpCopyMemory between two function floats carries Aligned 16. The
  // raw scan (even restricted to pointer-consuming opcodes) read the
  // alignment literal as a use of the pointer outside OpLoad.
  const r = applySpirvFix(fx('layer-copymemory-literal.spv'));
  assert.ok(r.applied.includes('fragment-layer-to-zero'), r.log.join('; '));
  const mod = parseSpirv(r.bytes);
  assert.ok(!mod.insns.some(i => i.op === 61 && i.words[2] === 16), 'no load of the Layer pointer may remain');
  // (No naga translate here: naga's SPIR-V frontend rejects
  // OpCopyMemory with a MemoryAccess operand outright -- the fixture
  // is spirv-val clean; the collision, not the copy, is the subject.)
});

test('rawscan F5 (analyzeRootDescriptors): a constant value equal to a stray PSB pointer type id is not an external use', () => {
  // The real root12 CBV module (servable, bound 67) plus a stray
  // PhysicalStorageBuffer pointer type %67 referenced by nothing,
  // and an OpConstant whose VALUE is 67. The raw scan read the
  // constant as the type being "used outside a root-descriptor cone"
  // and made the module unservable. (The same mutation applied at
  // the word level is spirv-val clean; verified in the rawscan lane.)
  const cbv = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-root-cbv.spv', import.meta.url)));
  const mod = parseSpirv(cbv);
  const firstFunc = mod.insns.findIndex(i => i.op === 54);
  assert.ok(firstFunc > 0);
  mod.insns.splice(firstFunc, 0,
    { op: 32, words: [67, 5349, 12], resultType: null, resultId: 67 },
    { op: 43, words: [6, 68, 67], resultType: 6, resultId: 68 });
  mod.header.bound = 69;
  const ra = analyzeRootDescriptors(mod);
  assert.equal(ra.moduleServable, true, ra.rejectReason || '');
  assert.equal(ra.rejectReason, null);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(fixed.applied.includes('root-descriptor-bda'), fixed.log.join('; '));
});

test('rawscan mirror pin: the bridge audit root check uses the operand-aware scan', () => {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  assert.ok(bridge.includes('idOperandUseIndex'), 'the bridge must carry the mirror helper');
  assert.ok(bridge.includes('idOperandUseIndex(m, u, tid)'), 'analyzeRoot must use it for the external-reference check');
});

test('rawscan F6 (R12S): an OpTypeInt width literal equal to the storage texel image load id is not a consumer', () => {
  // The load of the writeonly uimageBuffer is %32; %uint = OpTypeInt 32 0
  // carries the width literal 32. The pre-fix consumer scan (raw words
  // past the result id, metadata family skipped) refused the module
  // with 'storage texel image consumed by op 21'. The operand-aware
  // scan converts it; Dim flips Buffer -> 2D.
  const out = applySpirvFix(fx('storage-texel-width-collision.spv'));
  assert.ok(out.applied.includes('storage-texel-buffer-to-2d'), out.log.join('; '));
  const mod = parseSpirv(out.bytes);
  const img = mod.insns.find(i => i.op === 25);
  assert.equal(img.words[2], 1, 'Dim Buffer must become 2D');
  // No naga translation here: the image format is Unknown by design --
  // R14 injects it from the bound view at runtime (the real meta clear
  // carries StorageImageWriteWithoutFormat for the same reason).
});
