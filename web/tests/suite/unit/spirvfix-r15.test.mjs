// web/tests/suite/unit/spirvfix-r15.test.mjs — SPIR-V fix R15 (Route A)
// regression tests, campaign section 4.21.
//
// R15a specializes a runtime descriptor array with ONE provable index
// signature into a scalar clone (bw_arr_*); R15b re-roots read-only root
// descriptors (physical-storage-buffer pointers materialized from the
// push block) at synthesized storage bindings (bw_root_*). Both fail
// closed. The fixtures are REAL guest modules captured in the emulator
// (tools/vkd3d/probe/uav12.c and root12.c via web/tests/d3d12-dump.mjs):
//   testdata/spirv/d3d12-ps-uav-heap-array.spv — uav12 PS: runtime array
//     of storage images at set 5/binding 0, index = push@0 + 0.
//   testdata/spirv/d3d12-ps-root-cbv.spv — root12 CBV PS: the root CBV
//     VA is push-loaded as vec2<u32> and OpBitcast to a PSB pointer.
// The bridge audit mirrors these classifiers (source-pinned below).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  applySpirvFix, parseSpirv, serializeSpirv,
  analyzeDescriptorArrays, analyzeRootDescriptors, storageImageVars,
} from '../../../../tools/spirvfix/spirvfix.mjs';
import { scanWgslResources } from '../../../wgsl-utils.mjs';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';

const uavBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-uav-heap-array.spv', import.meta.url)));
const cbvBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-root-cbv.spv', import.meta.url)));
const uav64Bytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-uav-heap-array-int64.spv', import.meta.url)));

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

test('R15b fail-closed: a store through the root pointer keeps the module untouched', () => {
  const mod = parseSpirv(cbvBytes);
  const ret = mod.insns.findIndex(i => i.op === 253);
  assert.ok(ret > 0);
  // OpStore through chain %39 (a root-pointer cone chain), value %40.
  mod.insns.splice(ret, 0, { op: 62, words: [39, 40], resultType: null, resultId: null });
  const ra = analyzeRootDescriptors(mod);
  assert.equal(ra.moduleServable, false);
  assert.match(ra.rejectReason, /written through/);
  const fixed = applySpirvFix(serializeSpirv(mod));
  assert.ok(!fixed.applied.includes('root-descriptor-bda'));
  assert.ok(capsOf(parseSpirv(fixed.bytes)).has(5347), 'the loud rejection must stand');
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
});
