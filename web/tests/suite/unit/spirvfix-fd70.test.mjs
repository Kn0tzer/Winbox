// FD-70 — the second-wave Godot vertex modules (godot-vs-4000ddfd359efb05,
// godot-vs-ebac7e2d70a20d62), the descriptor-array orphan sweep's
// literal-reference defect, and the integer-offset root-addressing
// serving design (R15c).
//
// The pair's descriptor image array IS R15a-servable, but an OpLoad
// through a root-descriptor pointer carries an Aligned 16 memory-access
// literal that numerically equals the array type's id (%16), and the
// sweep counted the memory-access group's words as id references, so
// the specialized-away type never died: spirvfix kept the module
// un-translated one layer earlier (the post-specialization texel pass
// still saw the image type as array-owned and declined its clone), and
// the bridge audit refused RuntimeDescriptorArray for a module whose
// descriptor array it classifies servable. The sweep now bounds the
// memory-access family's scan at the group start and skips the
// literal-only OpTypeInt/OpTypeFloat words (mirrored in the bridge
// audit's skip5302 simulation; the native guard lives in
// tools/vkd3d/tests/vk64_native_test.cpp).
//
// With the false layers gone, the pair's root loads compute their
// addresses in integer space -- push-constant base VA plus an
// instance-derived index times the 176-byte element stride plus a
// field offset, added to the base's low half through the OpIAddCarry
// idiom and bitcast to a physical pointer. R15c serves exactly that
// shape: a slot carrying an integer-offset materialization is
// re-rooted through a word view (a synthesized storage struct wrapping
// a runtime array of 32-bit words at the same bw_root_p<off> name and
// binding), and every load through the slot is rewritten as word
// loads at its affine byte offset divided by four. The boundary tests
// below pin the class edges by model mutation of the captured module:
// a stride or field offset not divisible by four, and a store through
// an integer-offset pointer, all keep the loud refusal.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { applySpirvFix, parseSpirv, serializeSpirv, analyzeRootDescriptors } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

const load = name => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/${name}`, import.meta.url)));
const capsOf = mod => mod.insns.filter(i => i.op === 17).map(i => i.words[0]);
const rtasOf = mod => mod.insns.filter(i => i.op === 29);

for (const [name, loadCount] of [['godot-vs-4000ddfd359efb05.spv', 7], ['godot-vs-ebac7e2d70a20d62.spv', 5]]) {
  test(`FD-70 pair (${name}): integer-offset root loads are served through the word view`, () => {
    const a = analyzeRootDescriptors(parseSpirv(load(name)));
    assert.equal(a.moduleServable, true, 'the analysis serves the pair');
    assert.equal(a.slots.length, 1, 'one root slot');
    assert.equal(a.slots[0].wordView, true, 'the slot is served through the word view');
    assert.equal(a.slots[0].wordLoads.length, loadCount, 'every root load is a word load');
    const r = applySpirvFix(load(name));
    assert.ok(r.applied.includes('descriptor-array-specialize'), 'the descriptor array specializes');
    assert.ok(r.applied.includes('root-descriptor-bda'), 'the root loads are re-rooted');
    assert.ok(r.applied.includes('texel-buffer-to-2d'), 'the specialized texel clone converts in the post-specialization R12 pass');
    const mod = parseSpirv(r.bytes);
    const caps = capsOf(mod);
    assert.ok(!caps.includes(46), 'SampledBuffer capability is gone (the texel clone converted)');
    assert.ok(!caps.includes(4427), 'DrawParameters capability is gone');
    assert.ok(!caps.includes(5347), 'PhysicalStorageBufferAddresses is stripped (served, not merely declared)');
    assert.ok(!mod.insns.some(i => i.op === 32 && i.words[1] === 5349), 'no PhysicalStorageBuffer pointer type survives');
    const memModel = mod.insns.find(i => i.op === 14);
    assert.equal(memModel.words[0], 0, 'memory model is back to Logical addressing');
    const rootVar = mod.insns.find(i => i.op === 59 && (() => { const nm = mod.insns.find(n => n.op === 5 && n.words[0] === i.resultId); return nm && Buffer.from(nm.words.slice(1).flatMap(w => [w & 255, (w >> 8) & 255, (w >> 16) & 255, (w >> 24) & 255])).toString('utf8').startsWith('bw_root_p0'); })());
    assert.ok(rootVar, 'a variable named bw_root_p0 is synthesized');
    const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes }));
    assert.ok(wgsl.includes('bw_root_p0'), 'the translator accepts the module and the word view reaches WGSL');
  });
}

test('FD-70 pair: the sweep outcome does not depend on the colliding literal value', () => {
  const bytes = load('godot-vs-ebac7e2d70a20d62.spv');
  const plain = applySpirvFix(bytes);
  const mod = parseSpirv(bytes);
  let patched = 0;
  for (const ins of mod.insns) {
    // The root-pointer load whose Aligned literal (16) equals the
    // descriptor runtime-array type's id: rewrite the literal to a
    // value that names no id at all.
    if (ins.op === 61 && ins.words.length === 5 && ins.words[3] === 2 && ins.words[4] === 16) { ins.words[4] = 999; patched++; }
  }
  assert.equal(patched, 1, 'exactly one colliding load is patched');
  const viaLiteral = applySpirvFix(serializeSpirv(mod));
  assert.deepEqual(viaLiteral.applied, plain.applied, 'same rewrites with and without the collision');
  assert.equal(viaLiteral.bytes.length, plain.bytes.length, 'same output size with and without the collision');
});

// --- R15c boundary: model mutations of the captured module -----------------

function addUintConst(mod, value) {
  const uintTy = mod.insns.find(i => i.op === 21 && i.words[1] === 32 && i.words[2] === 0);
  const id = mod.header.bound++;
  const at = mod.insns.findIndex(i => i.op === 54);
  mod.insns.splice(at, 0, { op: 43, words: [uintTy.resultId, id, value], resultType: uintTy.resultId, resultId: id });
  return id;
}
function constValueOf(mod, id) {
  const ins = mod.insns.find(i => i.resultId === id);
  return ins && ins.op === 43 ? ins.words[2] : null;
}
function assertRefused(mod, label) {
  const a = analyzeRootDescriptors(mod);
  assert.equal(a.moduleServable, false, label + ': the analysis refuses');
  const r = applySpirvFix(serializeSpirv(mod));
  assert.ok(!r.applied.includes('root-descriptor-bda'), label + ': the rewrite does not fire');
  assert.ok(capsOf(parseSpirv(r.bytes)).includes(5347), label + ': the capability remains declared');
  assert.throws(
    () => naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes }),
    /PhysicalStorageBufferAddresses/,
    label + ': the translator refusal stands');
}

test('FD-70 boundary: an element stride not divisible by four keeps the refusal', () => {
  const mod = parseSpirv(load('godot-vs-ebac7e2d70a20d62.spv'));
  // The offset cone's IMul (index * 176): repoint its stride operand
  // at a fresh constant 174 (174 % 4 != 0, so the byte offset has no
  // exact word index).
  const mul = mod.insns.find(i => i.op === 132 && i.words.some(w => constValueOf(mod, w) === 176));
  assert.ok(mul, 'the stride multiply is found');
  const at = mul.words.findIndex(w => constValueOf(mod, w) === 176);
  mul.words[at] = addUintConst(mod, 174);
  assertRefused(mod, 'stride 174');
});

test('FD-70 boundary: a field offset not divisible by four keeps the refusal', () => {
  const mod = parseSpirv(load('godot-vs-ebac7e2d70a20d62.spv'));
  // The offset IAdd whose constant operand is the 48-byte field offset
  // and whose other operand is itself an IAdd (the accumulated index
  // term) -- distinct from the module's other 48-constant use.
  const add = mod.insns.find(i => i.op === 128 && i.words.some(w => constValueOf(mod, w) === 48)
    && i.words.some(w => { const d = mod.insns.find(x => x.resultId === w); return d && d.op === 128; }));
  assert.ok(add, 'the field-offset add is found');
  const at = add.words.findIndex(w => constValueOf(mod, w) === 48);
  add.words[at] = addUintConst(mod, 46);
  assertRefused(mod, 'field offset 46');
});

test('FD-70 boundary: a store through an integer-offset pointer keeps the refusal', () => {
  const mod = parseSpirv(load('godot-vs-ebac7e2d70a20d62.spv'));
  // The root loads are the function's Aligned loads, in order. Store
  // the first load's value back through the second load's chain: the
  // slot becomes writable, which the word view does not serve.
  const rootLoads = mod.insns.filter(i => i.op === 61 && i.words.length === 5);
  assert.ok(rootLoads.length >= 2, 'at least two root loads');
  const at = mod.insns.indexOf(rootLoads[1]);
  mod.insns.splice(at + 1, 0, { op: 62, words: [rootLoads[1].words[2], rootLoads[0].resultId], resultType: null, resultId: null });
  const a = analyzeRootDescriptors(mod);
  assert.match(a.rejectReason, /word view serves loads only/, 'the refusal names the loads-only word view');
  assertRefused(mod, 'store through a word-view chain');
});

for (const name of ['fd70-aligned-literal-sweep.spv', 'fd70-aligned-literal-twin.spv']) {
  test(`FD-70 synthetic (${name}): a servable array is swept clean despite literal collisions`, () => {
    const r = applySpirvFix(load(name));
    assert.deepEqual(r.applied, ['descriptor-array-specialize']);
    const mod = parseSpirv(r.bytes);
    assert.equal(rtasOf(mod).length, 0, 'no runtime array survives');
    assert.ok(!capsOf(mod).includes(5302), 'RuntimeDescriptorArray is stripped');
  });
}

test('FD-70 synthetic (fd70-aligned-literal-negative.spv): an unservable array keeps its refusal', () => {
  const r = applySpirvFix(load('fd70-aligned-literal-negative.spv'));
  assert.ok(!r.applied.includes('descriptor-array-specialize'), 'the unclassifiable-index array is not specialized');
  const mod = parseSpirv(r.bytes);
  assert.equal(rtasOf(mod).length, 1, 'the descriptor runtime array survives');
  assert.ok(capsOf(mod).includes(5302), 'RuntimeDescriptorArray remains declared');
});

test('FD-70 controls: previously admitted Godot modules keep byte-identical spirvfix output', () => {
  const pins = {
    'godot-vs-heap-plus-cbv-heap.spv': '0ee1291f1ef06c1bd560534476fd3ff8139ec99bb6831cbe376b8be1cedada15',
    'godot-vs-heap-plus-ssbo-offsets.spv': 'cab98702484b774dedc82212ec56c06bed850fa463c00d99a5a1d9750680fdac',
    'godot-fs-atomic-texel-heap.spv': '75f9e440766c0d96fa9d791acf6da4a85e669c47f2d91f34e5f46e0f8529f286',
  };
  for (const [name, sha] of Object.entries(pins)) {
    const r = applySpirvFix(load(name));
    assert.equal(createHash('sha256').update(r.bytes).digest('hex'), sha, `${name} output unchanged`);
  }
});

test('bridge mirror: the R15c integer-offset class exists in the audit', () => {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  assert.ok(bridge.includes('inline bool classifyDynOffset'), 'the bridge mirror classifies affine integer offsets');
  assert.ok(bridge.includes('inline bool wordCone('), 'the bridge mirror audits the word cone');
  assert.ok(bridge.includes('acc.wordView'), 'the bridge mirror marks integer-offset slots as word views');
  assert.ok(bridge.includes('h.hasDyn = true; h.dyn = dyn;'), 'the bridge mirror folds the carry addend into the address provenance');
});
