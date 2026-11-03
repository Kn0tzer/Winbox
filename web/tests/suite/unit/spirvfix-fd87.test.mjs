// FD-87 — the sibling modules that kept the Godot demo's second-wave
// pipeline creations failing on the FD-54+63+70+79/80 shipped build
// (deploy evidence: test-results/deploy-fd54-63-70-7980-20261102/
// godot-proof/console.txt). FD-70's own class was already extinct in
// that run (zero capability-5347 refusals); the three creations failed
// 0x80004005 solely on siblings:
//
//   godot-fs-f1b4e0027d98e2e4 (2,340 B, djb2 fe166085df72b93f) — refused
//   for SampledBuffer + RuntimeDescriptorArray. Its function body reads
//   only push constants and inputs; every descriptor variable it
//   declares (a texel-buffer runtime array, an SSBO-offsets block, a
//   bindless-CBV array) is never referenced. R18 strips the dead
//   descriptors and their orphaned types; R12X then drops the stray
//   texel capabilities and R18 the RuntimeDescriptorArray declaration.
//
//   godot-fs-f2b738c259a79b4a (369,400 B, djb2 d4da162f9cbdb948) —
//   refused for spirv-capability-unknown:29 (SampledImageArray-
//   DynamicIndexing, now named), RuntimeDescriptorArray and FD-72's
//   helper-invocation class. Its SampledBuffer share was NOT a
//   translator problem: the descriptor-array orphan sweep counted
//   GLSL.std.450 OpExtInst instruction numbers (13/14/29/30) as id
//   references, so the specialized-away texel array's types never
//   died, the image type stayed array-owned, and the
//   post-specialization texel conversion declined its clones. With
//   the ExtInst word bounded (FD-78's literal-position class, proven
//   live here), the conversion runs (508 fetches widened) and
//   SampledBuffer is genuinely served. What remains is exact: the 3D
//   image array whose two accesses index it with a value loaded
//   through a root-descriptor pointer (FD-88's data-dependent class,
//   unservable by specialization) keeps RuntimeDescriptorArray and,
//   as its passenger, the SampledImageArrayDynamicIndexing
//   declaration; the helper-invocation share is FD-72's.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applySpirvFix, parseSpirv, serializeSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

await naga.init();
const load = name => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/${name}`, import.meta.url)));
const capsOf = bytes => parseSpirv(bytes).insns.filter(i => i.op === 17).map(i => i.words[0]);
const toWgsl = bytes => String(naga.translate({ from: 'spirv', to: 'wgsl', source: bytes }));

test('FD-87 small module: dead descriptors stripped, module translates end to end', () => {
  const raw = load('godot-fs-f1b4e0027d98e2e4.spv');
  assert.ok(capsOf(raw).includes(46) && capsOf(raw).includes(5302), 'raw module carries the refused pair');
  assert.throws(() => toWgsl(raw), /SampledBuffer/, 'raw module is naga-refused');
  const out = applySpirvFix(raw);
  assert.ok(out.applied.includes('strip-dead-descriptors'), 'R18 fired: ' + out.applied.join(','));
  assert.deepEqual(capsOf(out.bytes), [1], 'only the Shader capability survives');
  const mod = parseSpirv(out.bytes);
  assert.equal(mod.insns.filter(i => i.op === 29).length, 0, 'no runtime array type survives');
  assert.equal(mod.insns.filter(i => i.op === 59 && [0, 2, 12].includes(i.words[2])).length, 0,
    'no descriptor-storage variable survives (the push-constant variable is untouched)');
  assert.ok(toWgsl(out.bytes).length > 0, 'the fixed module translates');
});

test('FD-87 guard: a USED descriptor array is never stripped as dead (multisig texel heap)', () => {
  const out = applySpirvFix(load('d3d12-cs-texel-heap-multisig.spv'));
  const dead = out.log.filter(l => l.includes('dead descriptor variable'));
  assert.equal(dead.length, 1, 'exactly the unused uniform block is stripped: ' + dead.join(' | '));
  assert.ok(out.applied.includes('descriptor-array-specialize'), 'the used texel array is specialized, not stripped');
  assert.ok(out.applied.includes('texel-buffer-to-2d'), 'and its fetches are converted');
});

test('FD-87 big module: SampledBuffer genuinely served via the ExtInst sweep bound', () => {
  const raw = load('godot-fs-f2b738c259a79b4a.spv');
  const out = applySpirvFix(raw);
  assert.ok(out.applied.includes('texel-buffer-to-2d'),
    'the post-specialization texel conversion runs (pre-fix it declined): ' + out.applied.join(','));
  assert.ok(out.log.some(l => l.includes('widened 508 fetch coordinate(s)')), 'all 508 texel fetches widened');
  const caps = capsOf(out.bytes);
  assert.ok(!caps.includes(46), 'SampledBuffer is gone from the fixed module');
  assert.ok(!caps.includes(5347), 'PhysicalStorageBufferAddresses is served by the root rewrites');
  // The characterized remainder (FD-88 + FD-72): the 3D image array
  // with data-dependent indices survives, so both declarations stay
  // and naga still refuses the module at the first of them.
  assert.ok(caps.includes(5302), 'RuntimeDescriptorArray stays (FD-88 array survives)');
  assert.ok(caps.includes(29), 'SampledImageArrayDynamicIndexing stays as its passenger');
  assert.throws(() => toWgsl(out.bytes), /SampledImageArrayDynamicIndexing/);
});

test('FD-87 R19: a stray SampledImageArrayDynamicIndexing declaration is stripped once no image array survives', () => {
  const base = applySpirvFix(load('d3d12-ps-texel-heap-array.spv')).bytes;
  const mod = parseSpirv(base);
  assert.ok(!capsOf(base).includes(29), 'base module carries no such declaration');
  // Re-declare the capability (the dxbc-spirv module-header form).
  const capAt = mod.insns.findIndex(i => i.op === 17);
  mod.insns.splice(capAt + 1, 0, { op: 17, words: [29], resultType: null, resultId: null });
  const stray = serializeSpirv(mod);
  assert.throws(() => toWgsl(stray), /SampledImageArrayDynamicIndexing/, 'the bare declaration is naga-refused');
  const out = applySpirvFix(stray);
  assert.ok(out.applied.includes('strip-stray-sampled-image-array-dynamic-indexing'), 'R19 fired');
  assert.ok(!capsOf(out.bytes).includes(29), 'the declaration is gone');
  assert.ok(toWgsl(out.bytes).length > 0, 'the module translates again');
});

test('FD-87 R19 boundary: the declaration survives while an image array survives', () => {
  const out = applySpirvFix(load('godot-fs-f2b738c259a79b4a.spv'));
  assert.ok(!out.applied.includes('strip-stray-sampled-image-array-dynamic-indexing'),
    'R19 must not fire while the FD-88 array survives');
});
