// web/tests/suite/unit/spirvfix-r8.test.mjs — SPIR-V fix R8 regression test.
//
// REGRESSION TEST for the D3D12/vkd3d-proton naga translation failure
// (scene12 complex-scene probe, 2026-10-09).
//
// R8: naga rejects OpCapability PhysicalStorageBufferAddresses (5346/5347)
//     and the SPV_KHR_physical_storage_buffer extension. vkd3d-proton emits
//     them for shaders that never actually use physical pointers (e.g. when
//     a descriptor table is present, the whole pipeline gets the
//     PhysicalStorageBuffer64 memory model). Fix: when unused, strip the
//     capability + extension and rewrite OpMemoryModel addressing to Logical.
//     When physical pointers ARE used, leave the module alone.
//
// This test constructs minimal SPIR-V modules exercising both cases and
// verifies applySpirvFix behavior (byte-level regression guard). Full naga
// translation is verified by the in-browser D3D12 scene12 probe.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applySpirvFix, parseSpirv, serializeSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';

// Minimal SPIR-V module builder for testing specific rewrites
function buildModule(insns, bound = 100) {
  const header = { magic: 0x07230203, version: 0x00010600, generator: 0, bound, schema: 0 };
  return { header, insns };
}

// OpCapability instruction: op=17, words=[capability]
const cap = (c) => ({ op: 17, words: [c] });
// OpMemoryModel: op=14, words=[addressingModel, memoryModel]
const memModel = (addr, mem) => ({ op: 14, words: [addr, mem] });
// OpExtension: op=10, words=[name...] (NUL-terminated, word-packed)
const ext = (name) => {
  const bs = [...name].map(c => c.charCodeAt(0));
  bs.push(0);
  while (bs.length % 4) bs.push(0);
  const words = [];
  for (let k = 0; k < bs.length; k += 4)
    words.push((bs[k] | (bs[k + 1] << 8) | (bs[k + 2] << 16) | (bs[k + 3] << 24)) >>> 0);
  return { op: 10, words };
};
// OpEntryPoint: op=15
const entryPoint = (model, id) => ({ op: 15, words: [model, id, 0x6e69616d, 0] }); // "main\0"

test('R8: unused PhysicalStorageBufferAddresses is stripped, memory model rewritten', () => {
  const mod = buildModule([
    cap(1),                                        // Shader
    cap(5347),                                     // PhysicalStorageBufferAddresses (vkd3d numbering)
    ext('SPV_KHR_physical_storage_buffer'),
    memModel(5348, 1),                             // PhysicalStorageBuffer64, Vulkan
    entryPoint(0, 1),                              // Vertex
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    result.applied.includes('strip-unused-physical-storage-buffer'),
    'R8 should strip unused PhysicalStorageBufferAddresses'
  );
  const outMod = parseSpirv(result.bytes);
  const hasCap = outMod.insns.some(i => i.op === 17 && (i.words[0] === 5346 || i.words[0] === 5347));
  assert.ok(!hasCap, 'PhysicalStorageBufferAddresses capability must be removed');
  const hasExt = outMod.insns.some(i => i.op === 10 &&
    Buffer.from(i.words.flatMap(w => [w & 255, (w >> 8) & 255, (w >> 16) & 255, (w >> 24) & 255]))
      .toString().startsWith('SPV_KHR_physical_storage_buffer'));
  assert.ok(!hasExt, 'SPV_KHR_physical_storage_buffer extension must be removed');
  const mm = outMod.insns.find(i => i.op === 14);
  assert.equal(mm.words[0], 0, 'addressing model must be rewritten to Logical');
  assert.equal(mm.words[1], 1, 'memory model must be preserved');
});

test('R8: module actually using physical pointers is left alone', () => {
  const mod = buildModule([
    cap(1),
    cap(5346),                                     // PhysicalStorageBufferAddresses (core numbering)
    ext('SPV_KHR_physical_storage_buffer'),
    memModel(5348, 1),
    entryPoint(0, 1),
    { op: 32, words: [10, 5349, 11] },             // OpTypePointer %10 PhysicalStorageBuffer %float(11) -- operands [resultId, storageClass, pointee]
    { op: 120, words: [11, 12, 13] },              // OpConvertUToPtr
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    !result.applied.includes('strip-unused-physical-storage-buffer'),
    'R8 must not strip when physical pointers are used'
  );
  const outMod = parseSpirv(result.bytes);
  const hasCap = outMod.insns.some(i => i.op === 17 && (i.words[0] === 5346 || i.words[0] === 5347));
  assert.ok(hasCap, 'capability must be preserved when in use');
});

test('R8: module without the capability passes through untouched', () => {
  const mod = buildModule([
    cap(1),
    memModel(0, 1),
    entryPoint(0, 1),
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    !result.applied.includes('strip-unused-physical-storage-buffer'),
    'R8 should not fire without the capability'
  );
});

test('R8: a pointer type whose RESULT ID is 5349 is not physical use (operand-index pin)', () => {
  // OpTypePointer operands are [resultId, storageClass, pointeeType]. The
  // used-test must read the storage class (words[1]); reading words[0]
  // (the result id) would refuse to strip this module and would miss a
  // real PhysicalStorageBuffer type with any other id. Route 0's bridge
  // audit mirrors this test exactly (vk64AuditSpirv).
  const mod = buildModule([
    cap(1),
    cap(5347),
    ext('SPV_KHR_physical_storage_buffer'),
    memModel(5348, 1),
    entryPoint(0, 1),
    { op: 32, words: [5349, 0, 11] },              // OpTypePointer %5349 UniformConstant %11 -- NOT a physical pointer
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    result.applied.includes('strip-unused-physical-storage-buffer'),
    'R8 must strip when the only 5349 is a pointer result id, not a storage class'
  );
});
