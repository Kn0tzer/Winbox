// web/tests/suite/unit/spirvfix-r15.test.mjs — SPIR-V fix R15 (Route A)
// regression tests, campaign section 4.21.
//
// R15a specializes a runtime descriptor array with ONE provable index
// signature into a scalar clone (bw_arr_*); R15b re-roots root
// descriptors (physical-storage-buffer pointers materialized from the
// push block) at synthesized storage bindings (bw_root_*), read-only or
// read-write when the slot is stored through (the bridge marks such
// ranges at draw record time and the section 4.30 BUFFER_READBACK path
// returns the bytes to guest memory; atomic RMW stays unserved). Both
// fail closed. The fixtures are REAL guest modules captured in the
// emulator (tools/vkd3d/probe/uav12.c and root12.c via
// web/tests/d3d12-dump.mjs):
//   testdata/spirv/d3d12-ps-uav-heap-array.spv — uav12 PS: runtime array
//     of storage images at set 5/binding 0, index = push@0 + 0.
//   testdata/spirv/d3d12-ps-uav-heap-array-write.spv — the WRITE shape of
//     the same heap array (the cs12 TEX wall): model-mutated from the
//     capture above with the minimal dxbc-spirv delta for a UAV store —
//     NonWritable dropped, image format Unknown -> Rgba8 (vkd3d knows the
//     UAV desc format, so cap 55 goes too), and an OpImageWrite of the
//     loaded texel back through the same element. Ids preserved (a spirv-as
//     text round-trip renumbers and is NOT how this fixture was made).
//   testdata/spirv/d3d12-ps-root-cbv.spv — root12 CBV PS: the root CBV
//     VA is push-loaded as vec2<u32> and OpBitcast to a PSB pointer.
//   testdata/spirv/d3d12-ps-root-srv.spv — root12 SRV PS: the root SRV
//     VA is rebuilt as a v2uint by the OpIAddCarry idiom, then bitcast.
//   testdata/spirv/d3d12-ps-root-uav.spv — root12 UAV PS: one bitcast
//     root VA, a runtime-index chain, and an OpStore through it.
//   testdata/spirv/d3d12-ps-uav-heap-array-smallid.spv — the uav12 PS
//     module above with ids 3 and 14 swapped (word-exact renumber; the
//     only literal words equal to 3 are the six pinned positions:
//     MemoryModel operand, two TypePointer storage classes, the
//     OpVariable storage class, an OpConstant value, and an
//     OpCompositeExtract index), so the array variable takes the small
//     id %3 and the module's own OpCompositeExtract literal index 3
//     collides with it. spirv-val clean. It pins the operand-aware use
//     scan (r15IdUseIndex / bridge findIdUse): a raw word scan misreads
//     that literal as a use of the variable and false-declines the
//     array ('array variable use op81').
// The bridge audit mirrors these classifiers (source-pinned below).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  applySpirvFix, parseSpirv, serializeSpirv,
  analyzeDescriptorArrays, analyzeRootDescriptors, storageImageVars, r15IdUseIndex,
} from '../../../../tools/spirvfix/spirvfix.mjs';
import { scanWgslResources } from '../../../wgsl-utils.mjs';
import * as naga from '../../../vendor/naga/node.js';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';

const uavBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-uav-heap-array.spv', import.meta.url)));
const cbvBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-root-cbv.spv', import.meta.url)));
const srvRootBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-root-srv.spv', import.meta.url)));
const uavRootBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-root-uav.spv', import.meta.url)));
const uav64Bytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-uav-heap-array-int64.spv', import.meta.url)));
const uavWriteBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-uav-heap-array-write.spv', import.meta.url)));
const uavSmallIdBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-uav-heap-array-smallid.spv', import.meta.url)));

function namesOf(mod) {
  const out = new Map();
  for (const ins of mod.insns) {
    if (ins.op !== 5) continue;
    const bytes = [];
    for (let k = 1; k < ins.words.length; k++) {
      const w = ins.words[k];
      bytes.push(w & 255, (w >> 8) & 255, (w >> 16) & 255, (w >> 24) & 255);
    }
    out.set(ins.words[0], Buffer.from(bytes).toString('utf8').replace(/\0.*$/, ''));
  }
  return out;
}
const capsOf = (mod) => new Set(mod.insns.filter(i => i.op === 17).map(i => i.words[0]));

test('R15a census: the real uav12 PS array is servable with signature push@0+0', () => {
  const { arrays } = analyzeDescriptorArrays(parseSpirv(uavBytes));
  assert.equal(arrays.length, 1);
  const a = arrays[0];
  assert.equal(a.set, 5); assert.equal(a.binding, 0); assert.equal(a.elemKind, 'image');
  assert.equal(a.servable, true, a.reason || '');
  assert.deepEqual(a.signatures[0].sig, { kind: 'push', byteOff: 0, add: 0 });
});

test('R15a rewrite: scalar clone replaces the array; cap 5302 is stripped', () => {
  const fixed = applySpirvFix(uavBytes);
  assert.ok(fixed.applied.includes('descriptor-array-specialize'), fixed.log.join('; '));
  const out = parseSpirv(fixed.bytes);
  assert.ok(!capsOf(out).has(5302), 'RuntimeDescriptorArray must be stripped once no array survives');
  assert.ok(!out.insns.some(i => i.op === 29), 'no OpTypeRuntimeArray may survive');
  const names = namesOf(out);
  assert.ok([...names.values()].includes('bw_arr_g5_b0_p0_a0'), 'clone carries its signature in its name');
  // The tier builds the R14 format signature from the ORIGINAL bytes:
  // the array-wrapped storage image must be visible at its set/binding.
  const siv = storageImageVars(uavBytes);
  assert.equal(siv.length, 1);
  assert.equal(siv[0].set, 5); assert.equal(siv[0].binding, 0);
});

test('R15a sweep precision: a capability number equal to the array type id keeps nothing alive', () => {
  // This real uav12 module declares Int64 (capability 11) and its
  // runtime-array TYPE also has id 11. A word-scan sweep that counts the
  // capability operand as a reference would keep the dead array type
  // (and the 5302 capability) alive forever; both sweeps (here and the
  // bridge mirror) skip header/directive instructions instead.
  const { arrays } = analyzeDescriptorArrays(parseSpirv(uav64Bytes));
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, true, arrays[0].reason || '');
  const fixed = applySpirvFix(uav64Bytes);
  assert.ok(fixed.applied.includes('descriptor-array-specialize'));
  const out = parseSpirv(fixed.bytes);
  assert.ok(!capsOf(out).has(5302), 'cap 5302 must be stripped once the only array is specialized');
  assert.ok(!out.insns.some(i => i.op === 29), 'no OpTypeRuntimeArray may survive');
});

test('R15a fail-closed: a second, different index signature defeats specialization', () => {
  const mod = parseSpirv(uavBytes);
  mod.header.bound = 300;
  const ret = mod.insns.findIndex(i => i.op === 253); // OpReturn
  assert.ok(ret > 0);
  // A second access chain on the array var (%14) with a CONSTANT index
  // (%50 == 3) beside the existing push@0 one: two signatures.
  mod.insns.splice(ret, 0,
    { op: 65, words: [29, 91, 14, 50], resultType: 29, resultId: 91 },
    { op: 61, words: [11, 92, 91], resultType: 11, resultId: 92 });
  const { arrays } = analyzeDescriptorArrays(mod);
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, /distinct index signatures/);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(!fixed.applied.includes('descriptor-array-specialize'));
});

test('R15a variant: push index plus a constant addend stays servable (signature +1)', () => {
  const mod = parseSpirv(uavBytes);
  mod.header.bound = 300;
  const chain = mod.insns.find(i => i.resultId === 30); // the array access chain
  assert.ok(chain && chain.op === 65);
  const at = mod.insns.indexOf(chain);
  mod.insns.splice(at, 0, { op: 128, words: [6, 91, 33, 25], resultType: 6, resultId: 91 }); // IAdd(indexLoad %33, const %25 == 1)
  chain.words[3] = 91;
  const { arrays } = analyzeDescriptorArrays(mod);
  assert.equal(arrays[0].servable, true, arrays[0].reason || '');
  assert.deepEqual(arrays[0].signatures[0].sig, { kind: 'push', byteOff: 0, add: 1 });
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(fixed.applied.includes('descriptor-array-specialize'));
  assert.ok([...namesOf(parseSpirv(fixed.bytes)).values()].includes('bw_arr_g5_b0_p0_a1'));
});

test('R15a census: a writable storage-image array is servable and flagged writable', () => {
  const { arrays } = analyzeDescriptorArrays(parseSpirv(uavWriteBytes));
  assert.equal(arrays.length, 1);
  const a = arrays[0];
  assert.equal(a.set, 5); assert.equal(a.binding, 0); assert.equal(a.elemKind, 'image');
  assert.equal(a.writable, true, 'no NonWritable on a Sampled==2 image array means writable');
  assert.equal(a.servable, true, a.reason || '');
  assert.deepEqual(a.signatures[0].sig, { kind: 'push', byteOff: 0, add: 0 });
  // The read fixture discriminates the flag: NonWritable-decorated, so not writable.
  const ro = analyzeDescriptorArrays(parseSpirv(uavBytes)).arrays[0];
  assert.equal(ro.writable, false);
  assert.equal(ro.servable, true, ro.reason || '');
});

test('R15a rewrite: a writable array specializes to a WRITABLE scalar clone', () => {
  const fixed = applySpirvFix(uavWriteBytes);
  assert.ok(fixed.applied.includes('descriptor-array-specialize'), fixed.log.join('; '));
  assert.ok(fixed.log.some(l => l.includes('(writable)')), 'the specialization log names writability');
  const out = parseSpirv(fixed.bytes);
  assert.ok(!capsOf(out).has(5302), 'RuntimeDescriptorArray must be stripped once no array survives');
  assert.ok(!out.insns.some(i => i.op === 29), 'no OpTypeRuntimeArray may survive');
  const names = namesOf(out);
  const clone = [...names.entries()].find(([, n]) => n === 'bw_arr_g5_b0_p0_a0');
  assert.ok(clone, 'clone carries its signature in its name');
  const decos = out.insns.filter(i => i.op === 71 && i.words[0] === clone[0]);
  assert.ok(decos.some(d => d.words[1] === 34 && d.words[2] === 5), 'clone keeps DescriptorSet 5');
  assert.ok(decos.some(d => d.words[1] === 33 && d.words[2] === 0), 'clone keeps Binding 0');
  assert.ok(!decos.some(d => d.words[1] === 24), 'clone must NOT gain NonWritable: it stays writable');
  assert.ok(out.insns.some(i => i.op === 99), 'the OpImageWrite survives, re-targeted at the loaded clone');
});

test('R15a naga: the specialized write module converts to a read_write storage texture with textureStore', () => {
  const fixed = applySpirvFix(uavWriteBytes);
  const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes }));
  assert.match(wgsl, /bw_arr_g5_b0_p0_a0_?: texture_storage_2d<rgba8unorm,\s*read_write>/,
    'naga emits the clone as a read_write storage texture (the tier parses this access for the layout)');
  assert.ok(wgsl.includes('textureStore'), 'the image write survives translation as textureStore');
});

test('R15a fail-closed: a writable array with a second index signature is left unchanged', () => {
  const mod = parseSpirv(uavWriteBytes);
  mod.header.bound = 300;
  const ret = mod.insns.findIndex(i => i.op === 253); // OpReturn
  assert.ok(ret > 0);
  mod.insns.splice(ret, 0,
    { op: 65, words: [29, 91, 14, 50], resultType: 29, resultId: 91 },
    { op: 61, words: [11, 92, 91], resultType: 11, resultId: 92 });
  const { arrays } = analyzeDescriptorArrays(mod);
  assert.equal(arrays[0].writable, true);
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, /distinct index signatures/);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(!fixed.applied.includes('descriptor-array-specialize'));
});

test('R15a fail-closed: a store through the element pointer defeats specialization', () => {
  const mod = parseSpirv(uavWriteBytes);
  const ret = mod.insns.findIndex(i => i.op === 253);
  assert.ok(ret > 0);
  // OpStore through chain %30 (the array element pointer), value %37.
  mod.insns.splice(ret, 0, { op: 62, words: [30, 37], resultType: null, resultId: null });
  const { arrays } = analyzeDescriptorArrays(mod);
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, /store through an array element/);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(!fixed.applied.includes('descriptor-array-specialize'));
});

test('R15a fail-closed: a texel-pointer use of the element is an escape', () => {
  const mod = parseSpirv(uavWriteBytes);
  mod.header.bound = 300;
  const ret = mod.insns.findIndex(i => i.op === 253);
  assert.ok(ret > 0);
  // OpImageTexelPointer(resultType %29, id %93, image = chain %30, coord %38, sample %37):
  // the element pointer escapes into a texel pointer -- no WGSL equivalent.
  mod.insns.splice(ret, 0, { op: 60, words: [29, 93, 30, 38, 37], resultType: 29, resultId: 93 });
  const { arrays } = analyzeDescriptorArrays(mod);
  assert.equal(arrays[0].servable, false);
  assert.match(arrays[0].reason, /escape \(op60\)/);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(!fixed.applied.includes('descriptor-array-specialize'));
});

test('R15b census: the real root12 CBV module is one read-only root slot at push@0', () => {
  const ra = analyzeRootDescriptors(parseSpirv(cbvBytes));
  assert.equal(ra.mats.length, 2);
  assert.ok(ra.mats.every(m => m.form === 'bitcast'), 'Wine DXBC form: OpBitcast, not OpConvertUToPtr');
  assert.equal(ra.moduleServable, true, ra.rejectReason || '');
  assert.deepEqual(ra.slots.map(s => s.byteOff), [0]);
});

test('R15b rewrite: re-rooted storage binding; 5346/5347 and the PSB model are gone', () => {
  const fixed = applySpirvFix(cbvBytes);
  assert.ok(fixed.applied.includes('root-descriptor-bda'), fixed.log.join('; '));
  const out = parseSpirv(fixed.bytes);
  const caps = capsOf(out);
  assert.ok(!caps.has(5346) && !caps.has(5347), 'physical-storage-buffer caps must be stripped');
  assert.ok(!out.insns.some(i => i.op === 32 && i.words[1] === 5349), 'no PSB pointer type may survive');
  const mm = out.insns.find(i => i.op === 14);
  assert.equal(mm.words[0], 0, 'addressing model must be Logical after the rewrite');
  const names = namesOf(out);
  const rootVar = [...names.entries()].find(([, n]) => n === 'bw_root_p0');
  assert.ok(rootVar, 'synthesized variable carries bw_root_p0');
  const varInsn = out.insns.find(i => i.op === 59 && i.resultId === rootVar[0]);
  assert.ok(varInsn && varInsn.words[2] === 12, 'root variable is StorageBuffer class');
  const decos = out.insns.filter(i => i.op === 71 && i.words[0] === rootVar[0]);
  assert.ok(decos.some(d => d.words[1] === 34 && d.words[2] === 0), 'root var in set 0 (no heap set in this module)');
  assert.ok(decos.some(d => d.words[1] === 33 && d.words[2] === 4096), 'root var at binding 4096 + pushOff/8');
});

test('R15b writable: a store through the root pointer is served, not rejected', () => {
  // Policy reversal, deliberate: this used to be the fail-closed case
  // ("no GPU->guest buffer writeback"). The section 4.30 BUFFER_READBACK
  // path plus draw-time GPU-write marking in the bridge removed that
  // premise, so an OpStore through a root cone now makes the slot
  // writable and servable; the fail-closed boundary moved to atomics,
  // escapes, and unresolvable address shapes (all still pinned below).
  const mod = parseSpirv(cbvBytes);
  const ret = mod.insns.findIndex(i => i.op === 253);
  assert.ok(ret > 0);
  // OpStore through chain %39 (a root-pointer cone chain), value %40.
  mod.insns.splice(ret, 0, { op: 62, words: [39, 40], resultType: null, resultId: null });
  const ra = analyzeRootDescriptors(mod);
  assert.equal(ra.moduleServable, true, ra.rejectReason || '');
  assert.equal(ra.slots.length, 1);
  assert.equal(ra.slots[0].writable, true);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(fixed.applied.includes('root-descriptor-bda'), fixed.log.join('; '));
  const out = parseSpirv(fixed.bytes);
  const rootVar = [...namesOf(out).entries()].find(([, n]) => n === 'bw_root_p0');
  assert.ok(rootVar, 'synthesized variable carries bw_root_p0');
  const decos = out.insns.filter(i => i.op === 71 && i.words[0] === rootVar[0]);
  assert.ok(!decos.some(d => d.words[1] === 24), 'a writable root variable carries no NonWritable decoration');
  assert.ok(out.insns.some(i => i.op === 62 && i.words[0] === 39), 'the store through the re-rooted chain survives');
});

test('R15b fail-closed: converting the root pointer back to an integer is an escape', () => {
  const mod = parseSpirv(cbvBytes);
  mod.header.bound = 300;
  const ret = mod.insns.findIndex(i => i.op === 253);
  assert.ok(ret > 0);
  mod.insns.splice(ret, 0, { op: 117, words: [6, 90, 37], resultType: 6, resultId: 90 }); // OpConvertPtrToU(bitcast %37)
  const ra = analyzeRootDescriptors(mod);
  assert.equal(ra.moduleServable, false);
  assert.match(ra.rejectReason, /escapes/);
});

test('R15b fail-closed: a VA that does not trace to the push block is not a root descriptor', () => {
  const mod = parseSpirv(cbvBytes);
  const chain = mod.insns.find(i => i.resultId === 31); // push chain feeding the VA load
  assert.ok(chain && chain.op === 65);
  chain.words[2] = 14; // re-root the chain at the fragment-output variable
  const ra = analyzeRootDescriptors(mod);
  assert.equal(ra.moduleServable, false);
  assert.match(ra.rejectReason, /does not trace/);
});

test('R15b fail-closed: a module-owned binding at the synthesized slot collides', () => {
  const mod = parseSpirv(cbvBytes);
  // Decorate the fragment input %14 as a descriptor variable occupying
  // set 0 / binding 4096 -- exactly where bw_root_p0 would be placed.
  const firstFunc = mod.insns.findIndex(i => i.op === 54);
  assert.ok(firstFunc > 0);
  mod.insns.splice(firstFunc, 0,
    { op: 71, words: [14, 34, 0], resultType: null, resultId: null },
    { op: 71, words: [14, 33, 4096], resultType: null, resultId: null });
  const ra = analyzeRootDescriptors(mod);
  assert.equal(ra.moduleServable, false);
  assert.match(ra.rejectReason, /collides with a module-owned binding/);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(!fixed.applied.includes('root-descriptor-bda'));
});

test('bridge mirror: array writability is classified by signature/uses, not by NonWritable', () => {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  assert.ok(!bridge.includes('writableBad'), 'the bridge mirror must not gate runtime arrays on NonWritable (spirvfix parity)');
  const fix = readFileSync(new URL('../../../../tools/spirvfix/spirvfix.mjs', import.meta.url), 'utf8');
  assert.ok(!fix.includes('storage-image array is writable (writes not served)'), 'the writable-array rejection is gone from spirvfix');
  assert.ok(fix.includes('rec.writable = '), 'spirvfix records writability on the array rec instead');
});

test('bridge mirror: the heap-set scan tests OpTypeRuntimeArray pointees', () => {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const at = bridge.indexOf('std::set<U32> heapSets;');
  assert.ok(at > 0, 'the bridge heap-set scan exists');
  const scan = bridge.slice(at, bridge.indexOf('heapSet = heapSets.size()', at));
  assert.ok(scan.includes('.op == 29'), 'the bridge heap-set scan must test OpTypeRuntimeArray (29) pointees, as spirvfix does');
  assert.ok(!scan.includes('.op == 48'), 'op 48 is OpSpecConstantTrue: never a pointer pointee type, so a scan testing it can never fire');
});

test('tier contract: scanWgslResources decodes bw_arr_* / bw_root_* names', () => {
  const wgsl = [
    '@group(5) @binding(0) var bw_arr_g5_b0_p0_a0: texture_storage_2d<rgba8unorm, read>;',
    '@group(0) @binding(4096) var<storage, read> bw_root_p0: CBV;',
    '@group(5) @binding(1) var bw_arr_g5_b1_c3: texture_storage_2d<rgba8unorm, read>;',
  ].join('\n');
  const decls = scanWgslResources(wgsl);
  const arr = decls.find(d => d.name === 'bw_arr_g5_b0_p0_a0');
  assert.deepEqual(arr.arrSig, { kind: 'push', pushOff: 0, add: 0 });
  const arrC = decls.find(d => d.name === 'bw_arr_g5_b1_c3');
  assert.deepEqual(arrC.arrSig, { kind: 'const', value: 3 });
  const root = decls.find(d => d.name === 'bw_root_p0');
  assert.equal(root.rootOff, 0);
});

test('tier contract: a read_write bw_root_* variable decodes its push offset', () => {
  const decls = scanWgslResources('@group(0) @binding(4097) var<storage, read_write> bw_root_p8: UAV;');
  const root = decls.find(d => d.name === 'bw_root_p8');
  assert.ok(root, 'read_write root variable is scanned');
  assert.equal(root.rootOff, 8);
});

test('protocol contract: ROOT_DESCS (0x1D) decodes into a rootDescs op keyed by push offset', () => {
  assert.equal(V2_REC.ROOT_DESCS, 0x1D);
  const payload = new Uint8Array(4 + 28);
  const d = new DataView(payload.buffer);
  d.setUint32(0, 1, true);
  d.setUint32(4, 0, true); // pushOff
  d.setBigUint64(8, 0x1234n, true); // buffer object id
  d.setBigUint64(16, 8192n, true); // absolute memory offset
  d.setBigUint64(24, 256n, true); // length
  const u32s = (...v) => { const b = new Uint8Array(v.length * 4); const dv = new DataView(b.buffer); v.forEach((x, i) => dv.setUint32(i * 4, x, true)); return b; };
  const frame = decodeV2Frame([
    { type: V2_REC.FRAME_BEGIN, payload: u32s(1, 64, 64, 0) },
    { type: V2_REC.ROOT_DESCS, payload },
    { type: V2_REC.FRAME_END, payload: u32s(1) },
  ]);
  const op = frame.passes.flatMap(p => p.ops).find(o => o.op === 'rootDescs');
  assert.ok(op, 'a rootDescs op must be produced');
  assert.deepEqual(op.entries.get(0), { obj: '0000000000001234', absOff: '8192', len: '256' });
});

test('bridge mirror: the audit admits 5302/5346/5347 only under the R15 classifiers', () => {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  assert.ok(bridge.includes('namespace r15 {'), 'bridge carries the r15 mirror classifiers');
  assert.ok(bridge.includes('if (c == 5302 && r15Arr.skip5302) continue;'), 'cap 5302 admitted only when every runtime array is specialized away');
  assert.ok(bridge.includes('if ((c == 5346 || c == 5347) && r15Root.servable) continue;'), 'caps 5346/5347 admitted only for servable root-descriptor modules');
  assert.ok(bridge.includes('V2_ROOT_DESCS = 0x1D'), 'bridge emits ROOT_DESCS records');
  assert.ok(bridge.includes('g_devVaRegistry'), 'bridge resolves root VAs through the devVa registry');
  const tier = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.ok(tier.includes("case 'rootDescs': st.rootDescs = op.entries;"), 'tier applies ROOT_DESCS state');
  assert.ok(tier.includes('declElement(st, d, k)'), 'tier resolves bw_arr elements from the push block');
  assert.ok(tier.includes('d.rootOff != null'), 'tier resolves bw_root bindings from ROOT_DESCS');
  assert.ok(bridge.includes('classifyRootAddr'), 'bridge mirrors the vec2 carry-idiom address classifier');
  assert.ok(bridge.includes('rootWritableSlots'), 'bridge censuses writable root slots onto the pipeline');
  assert.ok(bridge.includes('vk64MarkDrawRootWrites'), 'bridge marks writable root ranges when a draw is recorded');
  assert.ok(bridge.includes('atomicWrites'), 'bridge mirror splits atomic writes from plain stores');
});

// OpExtension strings start at words[0] (unlike OpName, which starts at words[1]).
const extNamesOf = (mod) => mod.insns.filter(i => i.op === 10).map(i => {
  const bytes = [];
  for (let k = 0; k < i.words.length; k++) {
    let w = i.words[k];
    for (let b = 0; b < 4; b++) { const c = w & 0xff; if (c === 0) return Buffer.from(bytes).toString('utf8'); bytes.push(c); w >>>= 8; }
  }
  return Buffer.from(bytes).toString('utf8');
});

test('R15b rewrite: the PSB extension is stripped with the PSB capability', () => {
  // The R15b strip decoded the OpExtension name from the wrong word
  // (the OpName offset), so the extension survived the rewrite and naga
  // rejected every root-descriptor module at page translation with
  // UnsupportedExtension("SPV_KHR_physical_storage_buffer").
  const fixed = applySpirvFix(cbvBytes);
  assert.ok(fixed.applied.includes('root-descriptor-bda'), fixed.log.join('; '));
  const out = parseSpirv(fixed.bytes);
  assert.ok(!extNamesOf(out).includes('SPV_KHR_physical_storage_buffer'), 'the PSB extension must not survive the rewrite');
  assert.ok(!capsOf(out).has(5346) && !capsOf(out).has(5347), 'no PSB capability may survive');
  assert.ok([...namesOf(out).values()].some(n => n.startsWith('bw_root_')), 'the re-rooted variable must survive');
});

test('R15b census: the real root12 SRV module traces through the OpIAddCarry idiom', () => {
  const ra = analyzeRootDescriptors(parseSpirv(srvRootBytes));
  assert.equal(ra.mats.length, 1);
  assert.equal(ra.mats[0].form, 'bitcast');
  assert.equal(ra.moduleServable, true, ra.rejectReason || '');
  assert.deepEqual(ra.slots.map(x => [x.byteOff, x.writable]), [[0, false]]);
});

test('R15b rewrite: the SRV carry cone is swept and the slot re-roots read-only', () => {
  const fixed = applySpirvFix(srvRootBytes);
  assert.ok(fixed.applied.includes('root-descriptor-bda'), fixed.log.join('; '));
  const out = parseSpirv(fixed.bytes);
  assert.ok(!out.insns.some(i => i.op === 149), 'no OpIAddCarry may survive the rewrite');
  assert.ok(!out.insns.some(i => i.op === 32 && i.words[1] === 5349), 'no PSB pointer type may survive');
  const rootVar = [...namesOf(out).entries()].find(([, n]) => n === 'bw_root_p0');
  assert.ok(rootVar, 'synthesized variable carries bw_root_p0');
  const decos = out.insns.filter(i => i.op === 71 && i.words[0] === rootVar[0]);
  assert.ok(decos.some(d => d.words[1] === 24), 'a read-only root variable keeps NonWritable');
  assert.ok(decos.some(d => d.words[1] === 33 && d.words[2] === 4096), 'root var at binding 4096');
});

test('R15b census: the real root12 UAV module is one writable root slot at push@0', () => {
  const ra = analyzeRootDescriptors(parseSpirv(uavRootBytes));
  assert.equal(ra.mats.length, 1);
  assert.equal(ra.moduleServable, true, ra.rejectReason || '');
  assert.deepEqual(ra.slots.map(x => [x.byteOff, x.writable]), [[0, true]]);
});

test('R15b rewrite: the UAV slot re-roots read-write and its store survives, Aligned stripped', () => {
  const fixed = applySpirvFix(uavRootBytes);
  assert.ok(fixed.applied.includes('root-descriptor-bda'), fixed.log.join('; '));
  const out = parseSpirv(fixed.bytes);
  const rootVar = [...namesOf(out).entries()].find(([, n]) => n === 'bw_root_p0');
  assert.ok(rootVar, 'synthesized variable carries bw_root_p0');
  const decos = out.insns.filter(i => i.op === 71 && i.words[0] === rootVar[0]);
  assert.ok(!decos.some(d => d.words[1] === 24), 'a writable root variable carries no NonWritable decoration');
  assert.ok(!out.insns.some(i => i.op === 32 && i.words[1] === 5349), 'no PSB pointer type may survive');
  // Walk access chains from every store's pointer operand: exactly one
  // store must bottom out at the synthesized root variable.
  const baseOf = new Map();
  for (const i of out.insns) {
    if ((i.op === 65 || i.op === 66 || i.op === 83) && i.resultId != null) baseOf.set(i.resultId, i.words[2]);
  }
  const rootOf = id => { let cur = id, guard = 0; while (baseOf.has(cur) && guard++ < 32) cur = baseOf.get(cur); return cur; };
  const rootStores = out.insns.filter(i => i.op === 62 && rootOf(i.words[0]) === rootVar[0]);
  assert.equal(rootStores.length, 1, 'exactly one store goes through the root variable');
  const st = rootStores[0];
  assert.ok(st.words.length < 3 || (st.words[2] & 2) === 0, 'the Aligned memory operand is stripped from the re-rooted store');
});

test('R15b fail-closed: a nonzero addend in the carry idiom is not served', () => {
  const mod = parseSpirv(srvRootBytes);
  mod.header.bound = 300;
  const carry = mod.insns.find(i => i.op === 149);
  assert.ok(carry, 'fixture carries an OpIAddCarry');
  const uintTy = mod.insns.find(i => i.op === 21 && i.words[1] === 32 && i.words[2] === 0);
  assert.ok(uintTy, 'fixture declares u32');
  const firstFunc = mod.insns.findIndex(i => i.op === 54);
  mod.insns.splice(firstFunc, 0, { op: 43, words: [uintTy.resultId, 200, 4], resultType: uintTy.resultId, resultId: 200 });
  carry.words[3] = 200; // IAddCarry(lo, 4): the address addend becomes 4
  const ra = analyzeRootDescriptors(mod);
  assert.equal(ra.moduleServable, false);
  assert.match(ra.rejectReason, /nonzero address addend/);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(!fixed.applied.includes('root-descriptor-bda'));
});

test('R15b fail-closed: an atomic through the root pointer is not served', () => {
  const probe = analyzeRootDescriptors(parseSpirv(uavRootBytes));
  const coneIds = new Set(probe.mats[0].uses.cone.map(c => c.resultId));
  assert.ok(coneIds.size > 0, 'fixture root cone has a chain');
  const mod = parseSpirv(uavRootBytes);
  const store = mod.insns.find(i => i.op === 62 && coneIds.has(i.words[0]));
  assert.ok(store, 'fixture stores through its root chain');
  store.op = 228; // OpAtomicStore: same pointer operand, atomic semantics
  const ra = analyzeRootDescriptors(mod);
  assert.equal(ra.moduleServable, false);
  assert.match(ra.rejectReason, /atomic/);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(!fixed.applied.includes('root-descriptor-bda'));
});

// ---------------------------------------------------------------------
// Operand-aware use scan (r15IdUseIndex; bridge mirror findIdUse).
// The R15 use scans used to treat ANY instruction word equal to the
// sought id as a use of it, so a literal operand colliding with a
// small id false-declined servable modules. The scan now walks only
// the words the opcode's grammar types as ids.

test('operand-aware scan: the small-id heap array classifies exactly like its unmutated twin', () => {
  const { arrays } = analyzeDescriptorArrays(parseSpirv(uavSmallIdBytes));
  assert.equal(arrays.length, 1);
  const a = arrays[0];
  assert.equal(a.varId, 3, 'the renumbered array variable is %3');
  assert.equal(a.set, 5); assert.equal(a.binding, 0); assert.equal(a.elemKind, 'image');
  // The module's OpCompositeExtract carries literal index 3; a raw word
  // scan reports 'array variable use op81' here.
  assert.equal(a.servable, true, a.reason || '');
  assert.deepEqual(a.signatures[0].sig, { kind: 'push', byteOff: 0, add: 0 });
});

test('operand-aware scan: the small-id array specializes to the same clone as its twin', () => {
  const fixed = applySpirvFix(uavSmallIdBytes);
  assert.ok(fixed.applied.includes('descriptor-array-specialize'), fixed.log.join('; '));
  const out = parseSpirv(fixed.bytes);
  assert.ok(!capsOf(out).has(5302), 'RuntimeDescriptorArray must be stripped once no array survives');
  assert.ok(!out.insns.some(i => i.op === 29), 'no OpTypeRuntimeArray may survive');
  assert.ok([...namesOf(out).values()].includes('bw_arr_g5_b0_p0_a0'), 'same clone name as the unmutated module');
});

test('operand-aware scan: a literal equal to the chain id is not a pointer escape', () => {
  const mod = parseSpirv(uavWriteBytes);
  const chain = mod.insns.find(i => i.op === 65 && i.resultId === 30);
  assert.ok(chain, 'write fixture access chain %30 exists');
  mod.header.bound = 300;
  const ret = mod.insns.findIndex(i => i.op === 253);
  assert.ok(ret > 0);
  // OpCompositeExtract of the loaded texel %37 with literal index 30 --
  // the chain's own id. A raw word scan reads the index as a use of the
  // chain pointer and reports 'array element pointer escape (op81)'.
  mod.insns.splice(ret, 0, { op: 81, words: [10, 90, 37, 30], resultType: 10, resultId: 90 });
  const { arrays } = analyzeDescriptorArrays(mod);
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, true, arrays[0].reason || '');
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(fixed.applied.includes('descriptor-array-specialize'), fixed.log.join('; '));
});

test('operand-aware scan: per-opcode operand layouts (literals are not id uses)', () => {
  const I = (op, words, resultType = null, resultId = null) => ({ op, words, resultType, resultId });
  const noCtx = { byId: new Map() };
  // OpSwitch width resolution needs the selector's defining instruction
  // and its integer type in ctx.byId.
  const switchCtx = (width) => ({ byId: new Map([
    [30, I(61, [20, 30, 40], 20, 30)],
    [20, I(21, [20, width, 0], null, 20)],
  ]) });
  const cases = [
    // [name, ctx, insn, sought id, expected index]
    ['CompositeExtract index is a literal', noCtx, I(81, [10, 50, 42, 7], 10, 50), 7, -1],
    ['CompositeExtract composite is an id', noCtx, I(81, [10, 50, 42, 7], 10, 50), 42, 2],
    ['CompositeExtract second index is a literal', noCtx, I(81, [10, 50, 42, 3, 7], 10, 50), 7, -1],
    ['CompositeInsert indexes are literals', noCtx, I(82, [10, 50, 42, 43, 7], 10, 50), 7, -1],
    ['CompositeInsert composite is an id', noCtx, I(82, [10, 50, 42, 43, 7], 10, 50), 43, 3],
    ['VectorShuffle components are literals', noCtx, I(79, [11, 51, 42, 43, 7, 0, 7, 1], 11, 51), 7, -1],
    ['VectorShuffle second vector is an id', noCtx, I(79, [11, 51, 42, 43, 7, 0, 7, 1], 11, 51), 43, 3],
    ['Load Aligned parameter is a literal', noCtx, I(61, [12, 52, 40, 2, 7], 12, 52), 7, -1],
    ['Load pointer is an id', noCtx, I(61, [12, 52, 40, 2, 7], 12, 52), 40, 2],
    ['Load MakePointerVisible scope is an id', noCtx, I(61, [12, 52, 40, 18, 4, 44], 12, 52), 44, 5],
    ['Load Aligned literal before a scope id', noCtx, I(61, [12, 52, 40, 18, 4, 44], 12, 52), 4, -1],
    ['Store Aligned parameter is a literal', noCtx, I(62, [40, 41, 2, 7]), 7, -1],
    ['Store object is an id', noCtx, I(62, [40, 41, 2, 7]), 41, 1],
    ['CopyMemory second-group Aligned is a literal', noCtx, I(63, [40, 41, 0, 2, 7]), 7, -1],
    ['CopyMemorySized Aligned is a literal', noCtx, I(64, [40, 41, 42, 2, 7]), 7, -1],
    ['CopyMemorySized size is an id', noCtx, I(64, [40, 41, 42, 2, 7]), 42, 2],
    ['Variable storage class is a literal', noCtx, I(59, [12, 53, 7], 12, 53), 7, -1],
    ['Variable initializer is an id', noCtx, I(59, [12, 53, 3, 44], 12, 53), 44, 3],
    ['Variable storage class 3 is not the initializer slot', noCtx, I(59, [12, 53, 3, 44], 12, 53), 3, -1],
    ['ExtInst instruction number is a literal', noCtx, I(12, [10, 54, 99, 7, 42], 10, 54), 7, -1],
    ['ExtInst set is an id', noCtx, I(12, [10, 54, 99, 7, 42], 10, 54), 99, 2],
    ['ExtInst operand is an id', noCtx, I(12, [10, 54, 99, 7, 42], 10, 54), 42, 4],
    ['ArrayLength member index is a literal', noCtx, I(68, [9, 55, 40, 7], 9, 55), 7, -1],
    ['GenericCastToPtrExplicit storage class is a literal', noCtx, I(123, [12, 56, 40, 7], 12, 56), 7, -1],
    ['LoopMerge control parameters are literals', noCtx, I(246, [60, 61, 1, 7]), 7, -1],
    ['LoopMerge continue label is an id', noCtx, I(246, [60, 61, 1, 7]), 61, 1],
    ['SelectionMerge control mask is a literal', noCtx, I(247, [60, 7]), 7, -1],
    ['BranchConditional weights are literals', noCtx, I(250, [40, 60, 61, 7, 7]), 7, -1],
    ['BranchConditional false label is an id', noCtx, I(250, [40, 60, 61, 7, 7]), 61, 2],
    ['Line line/column are literals', noCtx, I(8, [99, 7, 1234]), 7, -1],
    ['Line file is an id', noCtx, I(8, [99, 7, 1234]), 99, 0],
    ['ImageSampledImage operands mask is a literal', noCtx, I(87, [10, 57, 40, 41, 7, 42], 10, 57), 7, -1],
    ['ImageSampledImage bias parameter is an id', noCtx, I(87, [10, 57, 40, 41, 7, 42], 10, 57), 42, 5],
    ['ImageWrite operands mask is a literal', noCtx, I(99, [40, 41, 42, 7]), 7, -1],
    ['Switch 32-bit case literal is skipped', switchCtx(32), I(251, [30, 60, 7, 61, 9, 62]), 7, -1],
    ['Switch 32-bit case label is an id', switchCtx(32), I(251, [30, 60, 7, 61, 9, 62]), 61, 3],
    ['Switch 32-bit second literal is skipped', switchCtx(32), I(251, [30, 60, 7, 61, 9, 62]), 9, -1],
    ['Switch 64-bit case literal is two words', switchCtx(64), I(251, [30, 60, 7, 0, 61, 9, 0, 62]), 61, 4],
    ['Switch 64-bit first literal word is skipped', switchCtx(64), I(251, [30, 60, 7, 0, 61, 9, 0, 62]), 7, -1],
    ['Switch unresolvable width keeps the historical scan', noCtx, I(251, [30, 60, 7, 61]), 7, 2],
    ['unlisted opcode keeps the all-id scan', noCtx, I(83, [10, 58, 42], 10, 58), 42, 2],
  ];
  for (const [name, ctx, ins, id, want] of cases) {
    assert.equal(r15IdUseIndex(ctx, ins, id), want, name);
  }
});

test('bridge mirror: both R15 use scans route through the operand-aware helper', () => {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  assert.ok(bridge.includes('int hit = findIdUse(m, in, pid);'), 'bridge pointerUses uses findIdUse');
  assert.ok(bridge.includes('int hit = findIdUse(m, u, in.resultId);'), 'bridge analyzeArrays uses findIdUse');
  assert.ok(!bridge.includes('if (in.w[k] == pid)'), 'the raw word scan is gone from bridge pointerUses');
  assert.ok(!bridge.includes('if (u.w[k] == in.resultId)'), 'the raw word scan is gone from bridge analyzeArrays');
  assert.ok(bridge.includes('case 81:'), 'bridge findIdUse knows the OpCompositeExtract layout');
  assert.ok(bridge.includes('case 251:'), 'bridge findIdUse knows the OpSwitch layout');
  const fix = readFileSync(new URL('../../../../tools/spirvfix/spirvfix.mjs', import.meta.url), 'utf8');
  assert.ok(fix.includes('const hit = r15IdUseIndex(ctx, ins, pid);'), 'spirvfix r15PointerUses uses r15IdUseIndex');
  assert.ok(fix.includes('const hit = r15IdUseIndex(ctx, u, ins.resultId);'), 'spirvfix analyzeDescriptorArrays uses r15IdUseIndex');
  assert.ok(!fix.includes('if (ins.words[k] === pid)'), 'the raw word scan is gone from r15PointerUses');
  assert.ok(!fix.includes('if (u.words[k] === ins.resultId)'), 'the raw word scan is gone from analyzeDescriptorArrays');
});
