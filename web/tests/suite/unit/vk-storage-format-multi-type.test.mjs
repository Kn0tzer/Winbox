// web/tests/suite/unit/vk-storage-format-multi-type.test.mjs — FD-49:
// storage images without a format qualifier, where ONE descriptor-heap
// binding carries SEVERAL storage image types.
//
// vkd3d's descriptor heap declares its typed arrays at a single
// set/binding: a real Godot compute shader places a float storage array
// (read plane) and an int storage array (write plane) at set 5 /
// binding 0, both with Image Format Unknown and capabilities 55/56.
// Three stacked defects made the class untranslatable:
//   1. The R14 signature was keyed by set:binding alone, so it could
//      carry only ONE view format per binding -- whichever type it
//      named, the other stayed Unknown and naga rejected the module
//      (UnsupportedCapability StorageImageReadWithoutFormat). The
//      signature now carries per-type entries,
//      "set:binding@<typeKey>=fmt" (buildStorageSignature in
//      wgsl-utils.mjs, storageTypeKey in spirvfix), emitted only when
//      every resolving variable of the type agrees.
//   2. After R15a specialization, the two accessed arrays are two
//      scalar clones at the SAME binding with different types, which
//      naga validation rejects ("Bindings conflict with other
//      resource"). spirvfix specialized-binding-split moves clones
//      past the first to fresh bindings; the clone name keeps the
//      origin binding, which scanWgslResources records as origBinding
//      so bind-time resolution is unchanged.
//   3. One captured module is an empty shader (entry point is a bare
//      return) whose heap boilerplate declares two format-less storage
//      types nothing references; the translator processes type
//      declarations regardless, so it failed with
//      InvalidStorageImageWithoutFormat. spirvfix
//      prune-dead-storage-image-types removes exactly the unreferenced
//      declarations (live types keep failing loudly, test below).
//
// Fixtures are REAL guest modules captured from the Godot demo run
// (vkd3d shader dumps, test-results/fd16/godot-proof5/):
//   godot-cs-21fc48e37911e9a5.spv — float read @push+0, int write @push+3
//   godot-cs-40b0cb95dde9567e.spv — int @push+2, float @push+1
//   godot-cs-9347f1b2426929e0.spv — float read @push+0, int write @push+3
//   godot-cs-ba9896b53ac1c77f.spv — int @push+2, float @push+1
//   godot-cs-c9c003a7487b6126.spv — two int writes @push+2/@push+3 plus
//     an unaccessed float array (its type is dead and prunes)
//   godot-cs-e1ca507c951cf536.spv — the empty shader

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applySpirvFix, storageImageVars } from '../../../../tools/spirvfix/spirvfix.mjs';
import { buildStorageSignature, resolveStorageVarFormat, scanWgslResources } from '../../../wgsl-utils.mjs';
import * as naga from '../../../vendor/naga/node.js';

const load = (h) => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/godot-cs-${h}.spv`, import.meta.url)));
const RW = ['21fc48e37911e9a5', '40b0cb95dde9567e', '9347f1b2426929e0', 'ba9896b53ac1c77f', 'c9c003a7487b6126'];
const EMPTY = 'e1ca507c951cf536';
const KIND_FMT = { f: 4, i: 27, u: 39 }; // one naga-servable SPIR-V format per scalar kind
const BASE = 10;

// A synthetic descriptor heap for a module, built from its own
// reflection: one image-view entry per element the shader provably
// touches, its format chosen by the touching type's scalar kind -- the
// same shape of input the page tier resolves from a real bound heap.
function heapFor(vars) {
  const views = new Map();
  for (const v of vars) {
    if (!v.index) continue;
    const el = v.index.kind === 'const' ? v.index.value : BASE + v.index.add;
    views.set(el, 'V' + el + '_' + v.kind);
  }
  const bindings = [...views.entries()].map(([el, obj]) => ({ binding: 0, dstArrayElement: el, count: 1, kind: 2, obj }));
  const viewFormatOf = (obj) => KIND_FMT[obj.split('_')[1]] ?? null;
  const pushBlock = new Uint8Array(64);
  new DataView(pushBlock.buffer).setUint32(32, BASE, true);
  return { bindings, viewFormatOf, pushBlock };
}
function signatureFor(bytes) {
  const vars = storageImageVars(bytes);
  const heap = heapFor(vars);
  return buildStorageSignature(vars, (v) => resolveStorageVarFormat(heap.bindings, v, heap.pushBlock, heap.viewFormatOf));
}
const toWgsl = (bytes, storageFormats) => String(naga.translate({
  from: 'spirv', to: 'wgsl', source: applySpirvFix(bytes, { storageFormats }).bytes,
}));
const storageDecls = (wgsl) => wgsl.split('\n').filter((l) => l.includes('texture_storage'));

test('reflection: one heap binding carrying two storage types reports two type keys', () => {
  const vars = storageImageVars(load('21fc48e37911e9a5'));
  assert.equal(vars.length, 4); // two accessed arrays + two unaccessed aliases
  assert.ok(vars.every((v) => v.set === 5 && v.binding === 0));
  const keys = new Set(vars.map((v) => v.typeKey));
  assert.equal(keys.size, 2);
  assert.deepEqual(new Set(vars.map((v) => v.kind)), new Set(['f', 'i']));
  const accessed = vars.filter((v) => v.index);
  assert.deepEqual(accessed.map((v) => v.index.add).sort(), [0, 3]);
  assert.ok(accessed.every((v) => v.index.kind === 'push' && v.index.byteOff === 32));
});

test('signature: a multi-type binding resolves per type; a single-type binding keeps the plain form', () => {
  const sig = signatureFor(load('21fc48e37911e9a5'));
  const vars = storageImageVars(load('21fc48e37911e9a5'));
  const fkey = vars.find((v) => v.kind === 'f').typeKey;
  const ikey = vars.find((v) => v.kind === 'i').typeKey;
  assert.ok(sig.includes(`5:0@${fkey}=${KIND_FMT.f}`), sig);
  assert.ok(sig.includes(`5:0@${ikey}=${KIND_FMT.i}`), sig);
  // The pre-existing single-type module keeps byte-identical plain entries.
  const pass1 = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-cs-uav-heap-write-unknown-pass1.spv', import.meta.url)));
  const v1 = storageImageVars(pass1);
  const plain = buildStorageSignature(v1, () => 4);
  assert.ok(!plain.includes('@'), plain);
  assert.ok(plain.split(';').every((p) => /^\d+:\d+=4$/.test(p)), plain);
});

test('signature: disagreement between resolving variables omits the type; abstention does not', () => {
  const mk = (add) => ({ set: 5, binding: 0, format: 0, kind: 'f', typeKey: 'kf32d1x000', array: true, index: { kind: 'push', byteOff: 32, add } });
  const other = { set: 5, binding: 0, format: 0, kind: 'i', typeKey: 'ki32d1x000', array: true, index: { kind: 'push', byteOff: 32, add: 3 } };
  const disagree = buildStorageSignature([mk(0), mk(1), other], (v) => (v.typeKey === 'ki32d1x000' ? 27 : v.index.add === 0 ? 4 : 5));
  assert.ok(!disagree.includes('@kf32d1x000'), disagree); // 4 vs 5: unproven, omitted
  assert.ok(disagree.includes('@ki32d1x000=27'), disagree);
  const abstain = buildStorageSignature([mk(0), { ...mk(1), index: null }, other], (v) => (v.typeKey === 'ki32d1x000' ? 27 : v.index ? 4 : null));
  assert.ok(abstain.includes('@kf32d1x000=4'), abstain); // one proof, one abstention: served
});

test('end-to-end: all five read/write heap modules translate with per-type signatures', () => {
  for (const h of RW) {
    const bytes = load(h);
    const wgsl = toWgsl(bytes, signatureFor(bytes));
    const decls = scanWgslResources(wgsl).filter((d) => d.type.includes('texture_storage'));
    assert.equal(decls.length, 2, `${h}: expected two storage textures, got ${JSON.stringify(decls.map((d) => d.name))}`);
    assert.ok(decls[0].binding !== decls[1].binding, `${h}: the two storage textures must not share a binding`);
  }
});

test('end-to-end: the split clone binds through its origin binding', () => {
  const bytes = load('21fc48e37911e9a5');
  const wgsl = toWgsl(bytes, signatureFor(bytes));
  const decls = scanWgslResources(wgsl).filter((d) => d.type.includes('texture_storage'));
  assert.equal(decls.length, 2);
  const read = decls.find((d) => d.name.includes('_a0'));
  const write = decls.find((d) => d.name.includes('_a3'));
  assert.equal(read.binding, 0);
  assert.deepEqual(read.arrSig, { kind: 'push', pushOff: 32, add: 0 });
  assert.notEqual(write.binding, 0); // moved off the shared binding
  assert.equal(write.origBinding, 0); // ...but resolves descriptors at the origin binding
  assert.deepEqual(write.arrSig, { kind: 'push', pushOff: 32, add: 3 });
  assert.match(write.type, /write/);
});

test('end-to-end: the empty shader prunes its dead declarations and translates', () => {
  const bytes = load(EMPTY);
  const r = applySpirvFix(bytes, {});
  assert.ok(r.applied.includes('prune-dead-storage-image-types'), r.applied.join(','));
  const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes }));
  assert.equal(storageDecls(wgsl).length, 0);
});

test('fail-closed: the legacy one-format signature cannot resolve a two-type binding', () => {
  assert.throws(() => toWgsl(load('21fc48e37911e9a5'), '5:0=4'), /WithoutFormat/);
});

test('fail-closed: a partial typed signature leaves the other type Unknown', () => {
  const vars = storageImageVars(load('21fc48e37911e9a5'));
  const fkey = vars.find((v) => v.kind === 'f').typeKey;
  assert.throws(() => toWgsl(load('21fc48e37911e9a5'), `5:0@${fkey}=4`), /WithoutFormat/);
});

test('fail-closed: kind-mismatched typed formats are refused by the scalar-kind guard', () => {
  const vars = storageImageVars(load('21fc48e37911e9a5'));
  const fkey = vars.find((v) => v.kind === 'f').typeKey;
  const ikey = vars.find((v) => v.kind === 'i').typeKey;
  assert.throws(() => toWgsl(load('21fc48e37911e9a5'), `5:0@${fkey}=27;5:0@${ikey}=4`), /WithoutFormat/);
});

test('fail-closed: live format-less types are never pruned', () => {
  const bytes = load('21fc48e37911e9a5');
  const r = applySpirvFix(bytes, {});
  assert.ok(!r.applied.includes('prune-dead-storage-image-types'), r.applied.join(','));
  assert.throws(() => String(naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes })), /WithoutFormat/);
});
