// web/tests/suite/unit/vk-storage-format-inject.test.mjs — storage-image
// format injection for descriptor-ARRAY variables (the cs12 TEX wall).
//
// spirvfix R14 injects a bound view's format into an Unknown-format
// storage image type, but the page tier resolved the format from
// ELEMENT 0 of the binding. A guest heap table can place the UAV the
// shader actually writes at a nonzero element with a null view at
// element 0 -- the real cs12 compute module does exactly this (its
// second pass targets heap element 1) -- so nothing was injected and
// naga rejected the module with StorageImageWriteWithoutFormat,
// skipping the dispatch. The fix: storageImageVars reports a runtime
// array's proven index signature (the same analyzeDescriptorArrays
// analysis R15a serves on), and wgsl-utils resolveStorageVarFormat
// resolves the format of the element the shader will touch -- constant
// index, or push index evaluated against the draw's push block --
// fail-closed. Callers without a push block get a format only when
// every bound image element agrees on one.
//
// Fixtures are REAL guest modules captured from the cs12 probe's V2
// chunk stream (tools/vkd3d/probe/cs12.c, TEX case, first frame):
//   d3d12-cs-uav-heap-write-unknown-pass1.spv — array index push@0 + 0.
//   d3d12-cs-uav-heap-write-unknown-pass2.spv — array index push@0 + 1
//     (the failing shape), plus a set-3 sampled-image array.
// Both declare the storage image format Unknown with capability 56.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applySpirvFix, storageImageVars } from '../../../../tools/spirvfix/spirvfix.mjs';
import { resolveStorageVarFormat } from '../../../wgsl-utils.mjs';
import * as naga from '../../../vendor/naga/node.js';

const pass1 = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-cs-uav-heap-write-unknown-pass1.spv', import.meta.url)));
const pass2 = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-cs-uav-heap-write-unknown-pass2.spv', import.meta.url)));
const heapRead = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-uav-heap-array.spv', import.meta.url)));

const ZERO = '0000000000000000';
const VIEW_A = '0000000000564b01e0'; // the pass-2 UAV view in the capture
const VIEW_B = '0000000000564b01de';
const RGBA8 = 4, RGBA8_SNORM = 5, R8_UINT = 39; // SPIR-V Image Format values (vendored-naga numbering, per V2_STORAGE_FORMAT)
// Decoded DESC_SET entry shape (web/vk-protocol.mjs): one entry per
// written (binding, arrayElement); kind 2 = image view.
const img = (el, obj) => ({ binding: 0, dstArrayElement: el, count: 1, kind: 2, obj });
const viewFormatOf = (map) => (obj) => (Object.prototype.hasOwnProperty.call(map, obj) ? map[obj] : null);
const push = (u32) => { const b = new Uint8Array(64); new DataView(b.buffer).setUint32(0, u32, true); return b; };
const sigFor = (code, bindings, v, pb, vfo) => {
  const spv = resolveStorageVarFormat(bindings, v, pb, vfo);
  return spv == null ? '' : v.set + ':' + v.binding + '=' + spv;
};
const toWgsl = (bytes, storageFormats) => String(naga.translate({
  from: 'spirv', to: 'wgsl', source: applySpirvFix(bytes, { storageFormats }).bytes,
}));

test('reflection: array storage vars carry their proven index signature', () => {
  const v1 = storageImageVars(pass1);
  assert.equal(v1.length, 1);
  assert.equal(v1[0].format, 0);
  assert.equal(v1[0].array, true);
  assert.deepEqual(v1[0].index, { kind: 'push', byteOff: 0, add: 0 });
  const v2 = storageImageVars(pass2);
  assert.equal(v2.length, 1);
  assert.equal(v2[0].set, 5); assert.equal(v2[0].binding, 0);
  assert.equal(v2[0].format, 0);
  assert.equal(v2[0].array, true);
  assert.deepEqual(v2[0].index, { kind: 'push', byteOff: 0, add: 1 });
  // The uav12 heap-read module (the R15a fixture) reports its index too.
  const vr = storageImageVars(heapRead);
  assert.equal(vr[0].array, true);
  assert.deepEqual(vr[0].index, { kind: 'push', byteOff: 0, add: 0 });
});

test('resolver: the pass-2 element (index 1, null at 0) resolves from its own view', () => {
  const v = storageImageVars(pass2)[0];
  const bindings = [img(0, ZERO), img(1, VIEW_A)];
  const vfo = viewFormatOf({ [VIEW_A]: RGBA8 });
  assert.equal(resolveStorageVarFormat(bindings, v, push(0), vfo), RGBA8);
  // No push block (a caller that cannot supply one): the single non-null
  // element's format is the unique agreement.
  assert.equal(resolveStorageVarFormat(bindings, v, null, vfo), RGBA8);
});

test('resolver: element 0 shape (pass 1) still resolves', () => {
  const v = storageImageVars(pass1)[0];
  assert.equal(resolveStorageVarFormat([img(0, VIEW_A)], v, push(0), viewFormatOf({ [VIEW_A]: RGBA8 })), RGBA8);
});

test('resolver: the push value selects the element (descriptor-table start honored)', () => {
  const v = storageImageVars(pass2)[0]; // index = push@0 + 1
  const vfo = viewFormatOf({ [VIEW_A]: RGBA8 });
  // Table start 5 -> element 6.
  assert.equal(resolveStorageVarFormat([img(6, VIEW_A)], v, push(5), vfo), RGBA8);
  assert.equal(resolveStorageVarFormat([img(1, VIEW_A)], v, push(5), vfo), null);
});

test('resolver: unproven elements resolve nothing (fail-closed)', () => {
  const v = storageImageVars(pass2)[0];
  const vfo = viewFormatOf({ [VIEW_A]: RGBA8, [VIEW_B]: RGBA8_SNORM });
  // Element 1 not covered by any entry.
  assert.equal(resolveStorageVarFormat([img(0, VIEW_A)], v, push(0), vfo), null);
  // Element 1 covered by a null view write.
  assert.equal(resolveStorageVarFormat([img(0, VIEW_A), img(1, ZERO)], v, push(0), vfo), null);
  // Element 1's view format is unmappable in the tier.
  assert.equal(resolveStorageVarFormat([img(1, VIEW_A)], v, push(0), viewFormatOf({})), null);
  // No push block and the bound elements disagree on the format.
  assert.equal(resolveStorageVarFormat([img(0, VIEW_B), img(1, VIEW_A)], v, null, vfo), null);
  // An array with no proven index signature resolves nothing at all.
  assert.equal(resolveStorageVarFormat([img(0, VIEW_A)], { ...v, index: null }, push(0), vfo), null);
});

test('resolver: constant-index arrays resolve their constant element', () => {
  const v = { set: 5, binding: 0, format: 0, array: true, index: { kind: 'const', value: 2 } };
  const vfo = viewFormatOf({ [VIEW_A]: RGBA8, [VIEW_B]: RGBA8_SNORM });
  assert.equal(resolveStorageVarFormat([img(0, VIEW_B), img(2, VIEW_A)], v, null, vfo), RGBA8);
  assert.equal(resolveStorageVarFormat([img(0, VIEW_A)], v, null, vfo), null);
});

test('resolver: direct (non-array) variables keep the element-0 rule', () => {
  const v = { set: 0, binding: 0, format: 0 };
  const vfo = viewFormatOf({ [VIEW_A]: RGBA8 });
  assert.equal(resolveStorageVarFormat([img(0, VIEW_A)], v, null, vfo), RGBA8);
  assert.equal(resolveStorageVarFormat([img(0, ZERO)], v, null, vfo), null);
  assert.equal(resolveStorageVarFormat([], v, null, vfo), null);
});

test('end-to-end: pass 2 translates with the resolver-built signature', () => {
  const v = storageImageVars(pass2)[0];
  const sig = sigFor(pass2, [img(0, ZERO), img(1, VIEW_A)], v, push(0), viewFormatOf({ [VIEW_A]: RGBA8 }));
  assert.equal(sig, '5:0=4');
  const wgsl = toWgsl(pass2, sig);
  assert.match(wgsl, /texture_storage_2d<rgba8unorm, ?write>/);
  assert.match(wgsl, /textureStore/);
});

test('end-to-end: without a proven format the wall stands (no guess)', () => {
  assert.throws(() => toWgsl(pass2, ''), /StorageImageWriteWithoutFormat/);
  // A kind-mismatched format (uint format for a float-typed image) is
  // refused by R14 and the type stays Unknown.
  assert.throws(() => toWgsl(pass2, '5:0=' + R8_UINT), /StorageImageWriteWithoutFormat/);
});

test('end-to-end: the injected format is the element view\u2019s, not a constant', () => {
  const v = storageImageVars(pass2)[0];
  const sig = sigFor(pass2, [img(0, ZERO), img(1, VIEW_A)], v, push(0), viewFormatOf({ [VIEW_A]: RGBA8_SNORM }));
  assert.equal(sig, '5:0=5');
  const wgsl = toWgsl(pass2, sig);
  assert.match(wgsl, /texture_storage_2d<rgba8snorm, ?write>/);
});

test('end-to-end: pass 1 translates with its element-0 signature', () => {
  const v = storageImageVars(pass1)[0];
  const sig = sigFor(pass1, [img(0, VIEW_A)], v, push(0), viewFormatOf({ [VIEW_A]: RGBA8 }));
  assert.equal(sig, '5:0=4');
  assert.match(toWgsl(pass1, sig), /textureStore/);
});

test('tier wiring: vkwebgpu threads the draw push block into the signature', () => {
  const src = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.match(src, /function storageSignatureFor\(code, sets, pushBlock = null\)/);
  assert.match(src, /resolveStorageVarFormat\(set\.bindings \|\| \[\], v, pushBlock,/);
  assert.match(src, /storageSignatureFor\(vsmPre\.code, sets, pushBlock\)/);
  assert.match(src, /storageSignatureFor\(fsmPre\.code, sets, pushBlock\)/);
  assert.equal((src.match(/st\.sets, st\.pushBlock\)/g) || []).length, 3);
});
