// spirvfix FD-62 (serving): atomics (and loads/stores) performed
// through an OpImageTexelPointer formed on a descriptor-heap array
// element -- the captured Godot fragment module's shape
// (godot-fs-atomic-texel-heap.spv).
//
// The class is SERVED by the R12A rewrite (texelPointersToStorageBuffers
// in tools/spirvfix/spirvfix.mjs): a Dim=Buffer storage image is guest
// buffer memory, so a variable whose every use flows through texel
// pointers becomes a storage buffer of texels at the same set/binding,
// each OpImageTexelPointer becomes an OpAccessChain into it, and the
// loads/stores/atomics keep their instructions and land on the view's
// bytes. The classifier admits exactly the cones the rewrite serves,
// in both mirrors (analyzeDescriptorArrays here, r15::analyzeArrays in
// vk64bridge.cpp, with the bridge verdicts pinned in
// tools/vkd3d/tests/vk64_native_test.cpp):
//
//   * 32-bit-component scalar texel formats (R32f/i/ui, and the R64 /
//     sub-word / packed formats excluded by name), WGSL-expressible
//     integer atomics only (CompareExchangeWeak excluded: the
//     translator rejects the opcode itself);
//   * the image value never consumed except by formations and dead
//     loads (a mixed image/buffer element refuses), the texel pointer
//     never escaping (stored, passed, chained).
//
// The cone analysis also closes the FD-62 hardening's blind spot:
// VALUE-form formations (OpImageTexelPointer on an OpLoad result)
// were invisible to r15PointerUses, so such a module classified
// servable by accident and failed inside the translator. The
// value-form fixture below is now attributed (texelFormed counts it)
// and genuinely served end to end.
//
// The captured module itself: its texel class is fully eliminated and
// the transformed module carries both atomics as buffer atomics; its
// translation then stops only at the module's NEXT blocker, the
// BuiltIn HelperInvocation variable the vendored translator cannot
// ingest in any spelling (registered separately from FD-62).
//
// Fixtures are hand-authored SPIR-V assembly in the vkd3d heap idiom;
// sources in testdata/spirv/src/.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applySpirvFix, parseSpirv, analyzeDescriptorArrays } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../../web/vendor/naga/node.js';

await naga.init();

const load = name => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/${name}`, import.meta.url)));
const capsOf = mod => new Set(mod.insns.filter(i => i.op === 17).map(i => i.words[0]));
const opCount = (mod, op) => mod.insns.filter(i => i.op === op).length;
const toWgsl = bytes => String(naga.translate({ from: 'spirv', to: 'wgsl', source: bytes }));
const TEXEL_ATOMIC = /^texel-pointer atomic write through an array element is not served/;

test('captured Godot module: heap array servable, texel class eliminated, only the helper-invocation blocker remains', () => {
  const bytes = load('godot-fs-atomic-texel-heap.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  const heap = arrays.find(a => a.set === 4 && a.binding === 0);
  assert.ok(heap, 'the set-4 storage-image heap array must be classified');
  assert.equal(heap.elemKind, 'image');
  assert.equal(heap.writable, true);
  assert.equal(heap.servable, true);
  assert.equal(heap.texelFormed, 2);
  assert.equal(heap.texelAtomicWrites, 2);
  const out = applySpirvFix(bytes, {});
  assert.ok(out.applied.includes('descriptor-array-specialize'), out.log.join('\n'));
  assert.ok(out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  const outMod = parseSpirv(out.bytes);
  assert.equal(opCount(outMod, 60), 0, 'no OpImageTexelPointer may survive the rewrite');
  // No DESCRIPTOR runtime array survives: the only runtime arrays left
  // are buffer content (the module's CBV heap array and the SSBO_Offsets
  // block), which the tier serves through the standard buffer path with
  // the RuntimeDescriptorArray declaration retained (FD-61's shape).
  const byId = new Map(outMod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  for (const v of outMod.insns.filter(i => i.op === 59 && i.words[2] === 0)) {
    const pt = byId.get(v.words[0]);
    const pointee = pt && byId.get(pt.words[2]);
    assert.notEqual(pointee?.op, 29, 'no UniformConstant runtime-array variable may survive');
  }
  assert.ok(!capsOf(outMod).has(47), 'ImageBuffer must go with the converted image type');
  assert.equal(opCount(outMod, 241), 2, 'both OpAtomicOr sites survive as buffer atomics');
  // The sole remaining rejection is the module's next blocker (the
  // HelperInvocation builtin variable), not the texel class.
  assert.throws(() => toWgsl(out.bytes), /UnsupportedBuiltIn\(23\)/);
});

test('minimal reduction: chain -> texel pointer -> OpAtomicOr is served end to end', () => {
  const bytes = load('atomic-texel-heap.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, true);
  assert.equal(arrays[0].texelFormed, 1);
  assert.equal(arrays[0].texelAtomicWrites, 1);
  assert.equal(arrays[0].texelStoreWrites, 0);
  const out = applySpirvFix(bytes, {});
  assert.ok(out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.equal(opCount(parseSpirv(out.bytes), 60), 0);
  assert.match(toWgsl(out.bytes), /atomicOr\(/, 'the atomic must land on the storage buffer');
});

test('sabotage twin served: a texel pointer formed on an OpCopyObject of the chain', () => {
  const bytes = load('atomic-texel-heap-copy.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, true);
  assert.equal(arrays[0].texelAtomicWrites, 1);
  const out = applySpirvFix(bytes, {});
  assert.ok(out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.match(toWgsl(out.bytes), /atomicOr\(/);
});

test('value form: a formation through the loaded image value is attributed and served', () => {
  const bytes = load('atomic-texel-heap-valueform.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, true);
  assert.equal(arrays[0].texelFormed, 1, 'the value-form formation must be counted (it was invisible before)');
  assert.equal(arrays[0].texelAtomicWrites, 1);
  const out = applySpirvFix(bytes, {});
  assert.ok(out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.equal(opCount(parseSpirv(out.bytes), 60), 0);
  assert.match(toWgsl(out.bytes), /atomicOr\(/);
});

test('loads and stores through texel pointers are served without atomics', () => {
  const bytes = load('atomic-texel-heap-store.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays[0].servable, true);
  assert.equal(arrays[0].texelFormed, 2);
  assert.equal(arrays[0].texelStoreWrites, 2);
  const out = applySpirvFix(bytes, {});
  assert.ok(out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.equal(opCount(parseSpirv(out.bytes), 60), 0);
  toWgsl(out.bytes);
});

test('attribution: the benign writable array in the same module stays servable, both convert', () => {
  const bytes = load('atomic-texel-heap-twoarrays.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays.length, 2);
  const a = arrays.find(x => x.set === 2);
  const b = arrays.find(x => x.set === 4);
  assert.equal(a.servable, true, 'the OpImageWrite array must stay servable');
  assert.equal(a.texelFormed, 0);
  assert.equal(b.servable, true);
  assert.equal(b.texelAtomicWrites, 1);
  const out = applySpirvFix(bytes, {});
  assert.ok(out.applied.includes('storage-texel-buffer-to-2d'), out.log.join('\n'));
  assert.ok(out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.ok(!capsOf(parseSpirv(out.bytes)).has(5302), 'both arrays served: RuntimeDescriptorArray goes');
  assert.match(toWgsl(out.bytes), /atomicOr\(/);
});

test('direct variable: a texel-pointer atomic on a lone storage texel buffer is served pre-specialization', () => {
  const bytes = load('atomic-texel-direct.spv');
  const out = applySpirvFix(bytes, {});
  assert.ok(out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.equal(opCount(parseSpirv(out.bytes), 60), 0);
  assert.match(toWgsl(out.bytes), /atomicOr\(/);
});

test('refusal: OpAtomicCompareExchangeWeak through a texel pointer keeps the named refusal', () => {
  const bytes = load('atomic-texel-heap-weak.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, TEXEL_ATOMIC);
  assert.equal(arrays[0].texelAtomicWrites, 1);
  const out = applySpirvFix(bytes, {});
  assert.ok(!out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.ok(capsOf(parseSpirv(out.bytes)).has(5302), 'the refused array keeps RuntimeDescriptorArray');
  assert.throws(() => toWgsl(out.bytes), 'the translator must still reject the refused module');
});

test('refusal: a sub-word texel format (R8ui) keeps the named refusal', () => {
  const bytes = load('atomic-texel-heap-r8ui.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, TEXEL_ATOMIC);
  const out = applySpirvFix(bytes, {});
  assert.ok(!out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.throws(() => toWgsl(out.bytes));
});

test('refusal: a 64-bit texel format (R64ui) keeps the named refusal', () => {
  const bytes = load('atomic-texel-heap-r64ui.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, TEXEL_ATOMIC);
  const out = applySpirvFix(bytes, {});
  assert.ok(!out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.throws(() => toWgsl(out.bytes));
});

test('refusal: an element used both as an image and through a texel pointer keeps the named refusal', () => {
  const bytes = load('atomic-texel-heap-mixed.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, TEXEL_ATOMIC);
  const out = applySpirvFix(bytes, {});
  assert.ok(!out.applied.includes('texel-pointer-to-storage-buffer'), out.log.join('\n'));
  assert.ok(capsOf(parseSpirv(out.bytes)).has(5302));
  assert.throws(() => toWgsl(out.bytes));
});

test('escape preserved: a texel pointer stored as a value refuses as an escape, not as a texel use', () => {
  const { arrays } = analyzeDescriptorArrays(parseSpirv(load('atomic-texel-heap-escape.spv')));
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, false);
  assert.equal(arrays[0].reason, 'array element pointer escape (pointer stored as a value)');
  assert.equal(arrays[0].texelAtomicWrites, 0);
});
