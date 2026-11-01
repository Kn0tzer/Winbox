// FD-70 — the second-wave Godot vertex modules (godot-vs-4000ddfd359efb05,
// godot-vs-ebac7e2d70a20d62) and the descriptor-array orphan sweep's
// literal-reference defect.
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
// What remains for the pair after the fix is the true boundary: the
// vendored translator rejects PhysicalStorageBufferAddresses outright
// once the false layers are gone.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { applySpirvFix, parseSpirv, serializeSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

const load = name => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/${name}`, import.meta.url)));
const capsOf = mod => mod.insns.filter(i => i.op === 17).map(i => i.words[0]);
const rtasOf = mod => mod.insns.filter(i => i.op === 29);

for (const name of ['godot-vs-4000ddfd359efb05.spv', 'godot-vs-ebac7e2d70a20d62.spv']) {
  test(`FD-70 pair (${name}): false layers peeled, physical addressing is the remaining rejection`, () => {
    const r = applySpirvFix(load(name));
    assert.ok(r.applied.includes('descriptor-array-specialize'), 'the descriptor array specializes');
    assert.ok(r.applied.includes('texel-buffer-to-2d'), 'the specialized texel clone converts in the post-specialization R12 pass');
    const mod = parseSpirv(r.bytes);
    const caps = capsOf(mod);
    assert.ok(!caps.includes(46), 'SampledBuffer capability is gone (the texel clone converted)');
    assert.ok(!caps.includes(4427), 'DrawParameters capability is gone');
    assert.ok(caps.includes(5347), 'PhysicalStorageBufferAddresses remains declared (used form, not stripped)');
    const memModel = mod.insns.find(i => i.op === 14);
    assert.equal(memModel.words[0], 5348, 'memory model remains PhysicalStorageBuffer64');
    const rtas = rtasOf(mod);
    assert.equal(rtas.length, 3, 'only the three buffer-content runtime arrays survive');
    for (const t of rtas) {
      const elem = mod.insns.find(i => i.resultId === t.words[1]);
      assert.notEqual(elem.op, 25, 'no surviving runtime array has an image element (the descriptor array is swept)');
    }
    assert.throws(
      () => naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes }),
      /PhysicalStorageBufferAddresses/,
      'the translator remaining rejection names the physical addressing capability');
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
