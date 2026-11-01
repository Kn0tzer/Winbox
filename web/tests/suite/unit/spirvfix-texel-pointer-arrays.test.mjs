// spirvfix FD-62: atomics (and loads/stores) performed through an
// OpImageTexelPointer formed on a descriptor-heap array element.
//
// The captured Godot fragment module (godot-fs-atomic-texel-heap.spv,
// register FD-62) indexes a writable storage-image heap array and runs
// OpAtomicOr through OpImageTexelPointer on the elements. The uses
// analysis now audits THROUGH the texel pointer in both mirrors
// (r15PointerUses here, r15::pointerUses in vk64bridge.cpp): the texel
// pointer's loads / stores / atomics are image-CONTENT reads and writes,
// counted separately from writes to the descriptor slot itself, while
// the texel pointer VALUE escaping (stored, passed on) stays an escape.
//
// The verdict stays REFUSED, now for the true, named reason: the
// vendored translator rejects OpImageTexelPointer outright (it refuses
// the Image pointer storage class for every backend, and Buffer-dim
// images fail even earlier with UnsupportedCapability(ImageBuffer) --
// measured against the vendored naga 30.0.1, 2026-11-01). Before this
// classification the same module refused only by accident, as a generic
// 'array element pointer escape (op60)': any future change that taught
// the classifier to admit the formation without auditing its uses
// would have flipped the module to served at the audit and failed it
// later, quieter, inside the translator. These tests pin the named
// refusal, the per-array attribution, the copy-following sabotage
// direction, and the escape distinction. The bridge-side verdicts on
// the same fixture bytes are pinned in
// tools/vkd3d/tests/vk64_native_test.cpp.
//
// Fixtures are hand-authored SPIR-V assembly in the vkd3d heap idiom,
// spirv-as / spirv-val clean; sources in testdata/spirv/src/.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applySpirvFix, parseSpirv, analyzeDescriptorArrays } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../../web/vendor/naga/node.js';

await naga.init();

const load = name => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/${name}`, import.meta.url)));
const capsOf = mod => new Set(mod.insns.filter(i => i.op === 17).map(i => i.words[0]));
const TEXEL_ATOMIC = /^texel-pointer atomic write through an array element is not served/;

test('captured Godot module: the heap array refuses with the named texel-pointer reason', () => {
  const bytes = load('godot-fs-atomic-texel-heap.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  const heap = arrays.find(a => a.set === 4 && a.binding === 0);
  assert.ok(heap, 'the set-4 storage-image heap array must be classified');
  assert.equal(heap.elemKind, 'image');
  assert.equal(heap.writable, true);
  assert.equal(heap.servable, false);
  assert.match(heap.reason, TEXEL_ATOMIC);
  assert.ok(heap.texelAtomicWrites >= 1, 'the atomic through the texel pointer must be counted, got ' + heap.texelAtomicWrites);
  // The refusal is end-to-end: the fix tier does not specialize the
  // array, the RuntimeDescriptorArray capability survives, and the
  // translator still rejects the module -- refused loudly at the
  // audit, never half-served.
  const out = applySpirvFix(bytes, {});
  assert.ok(!out.applied.includes('descriptor-array-specialize'), out.log.join('\n'));
  assert.ok(capsOf(parseSpirv(out.bytes)).has(5302), 'RuntimeDescriptorArray must survive: the array is not served');
  assert.throws(() => naga.translate({ from: 'spirv', to: 'wgsl', source: out.bytes }),
    'the translator must still reject the module (the named blocker)');
});

test('minimal reduction: chain -> texel pointer -> OpAtomicOr refuses with the texel reason and counts', () => {
  const { arrays } = analyzeDescriptorArrays(parseSpirv(load('atomic-texel-heap.spv')));
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, TEXEL_ATOMIC);
  assert.equal(arrays[0].texelFormed, 1);
  assert.equal(arrays[0].texelAtomicWrites, 1);
  assert.equal(arrays[0].texelStoreWrites, 0);
});

test('sabotage: a texel pointer formed on an OpCopyObject of the chain refuses identically', () => {
  // A classifier that only audits a texel pointer formed directly on
  // the access-chain id would admit this module while the atomic
  // writes the element unexamined.
  const { arrays } = analyzeDescriptorArrays(parseSpirv(load('atomic-texel-heap-copy.spv')));
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, TEXEL_ATOMIC);
  assert.equal(arrays[0].texelAtomicWrites, 1);
});

test('attribution: the benign writable array in the same module stays servable', () => {
  const bytes = load('atomic-texel-heap-twoarrays.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays.length, 2);
  const a = arrays.find(x => x.set === 2);
  const b = arrays.find(x => x.set === 4);
  assert.equal(a.servable, true, 'the OpImageWrite array must stay servable');
  assert.equal(a.texelFormed, 0);
  assert.equal(b.servable, false);
  assert.match(b.reason, TEXEL_ATOMIC);
  assert.equal(b.texelAtomicWrites, 1);
  // The fix tier may specialize the servable array, but the refused
  // array is not silently dropped into served state: its runtime
  // array (and the capability) survives for the audit to refuse.
  const out = applySpirvFix(bytes, {});
  assert.ok(capsOf(parseSpirv(out.bytes)).has(5302), 'the refused array must keep RuntimeDescriptorArray for the audit');
});

test('escape preserved: a texel pointer stored as a value refuses as an escape, not as a texel use', () => {
  const { arrays } = analyzeDescriptorArrays(parseSpirv(load('atomic-texel-heap-escape.spv')));
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, false);
  assert.equal(arrays[0].reason, 'array element pointer escape (pointer stored as a value)');
  assert.equal(arrays[0].texelAtomicWrites, 0);
});
