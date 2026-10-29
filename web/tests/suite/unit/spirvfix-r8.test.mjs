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
import * as fs from 'node:fs';
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

// ---------------------------------------------------------------------------
// R8 downgrade census (campaign 4.43, lane-r8downgrade): the used-test above
// proves no pointer is DEREFERENCED through the physical model, but a
// module can still owe its validity to the physical addressing model in
// four further ways (each proven against spirv-val during the census: the
// module validates as PhysicalStorageBuffer64 and is rejected after the
// downgrade). R8 must DECLINE the downgrade by name for those shapes and
// leave the module physical; it must keep stripping the probed
// non-hazards. The bridge audit mirrors the census (vk64AuditSpirv).

const PSB_BASE = () => [
  cap(1),                                        // Shader
  cap(5347),                                     // PhysicalStorageBufferAddresses
  ext('SPV_KHR_physical_storage_buffer'),
  memModel(5348, 1),                             // PhysicalStorageBuffer64, Vulkan
  entryPoint(0, 1),                              // Vertex
];
const F32 = { op: 22, words: [20, 32] };         // %20 = OpTypeFloat 32
const VOID = { op: 19, words: [23] };            // %23 = OpTypeVoid
const BOOL = { op: 20, words: [24] };            // %24 = OpTypeBool
const ptrTy = (id, sc, pointee) => ({ op: 32, words: [id, sc, pointee] }); // OpTypePointer %id
const varIns = (ty, id, sc) => ({ op: 59, words: [ty, id, sc] });          // OpVariable
// A void function taking one pointer parameter of type %ptrId.
const callee = (fnTy, fn, param, ptrId) => [
  { op: 33, words: [fnTy, 23, ptrId] },          // OpTypeFunction void(ptr)
  { op: 54, words: [23, fn, 0, fnTy] },          // OpFunction %fn
  { op: 55, words: [ptrId, param] },             // OpFunctionParameter %param
  { op: 248, words: [90] },                      // OpLabel
  { op: 253, words: [] },                        // OpReturn
  { op: 56, words: [] },                         // OpFunctionEnd
];
// A void() function whose body is the given instructions.
const callerWith = (fnTy, fn, bodyInsns) => [
  { op: 33, words: [fnTy, 23] },                 // OpTypeFunction void()
  { op: 54, words: [23, fn, 0, fnTy] },          // OpFunction %fn
  { op: 248, words: [91] },                      // OpLabel
  ...bodyInsns,
  { op: 253, words: [] },                        // OpReturn
  { op: 56, words: [] },                         // OpFunctionEnd
];
const call = (id, fn, arg) => ({ op: 57, words: [23, id, fn, arg] }); // OpFunctionCall

const run = (insns) => applySpirvFix(serializeSpirv(buildModule(insns)));
const STRIP = 'strip-unused-physical-storage-buffer';
const assertDeclined = (r, what) => {
  assert.ok(!r.applied.includes(STRIP), `${what}: R8 must not strip (applied: ${r.applied})`);
  assert.ok(r.log.some(l => l.includes('downgrade declined')),
    `${what}: the decline must be named in the log (log: ${r.log.join(' | ')})`);
  const out = parseSpirv(r.bytes);
  assert.ok(out.insns.some(i => i.op === 17 && (i.words[0] === 5346 || i.words[0] === 5347)),
    `${what}: the PhysicalStorageBufferAddresses capability must be preserved`);
  assert.equal(out.insns.find(i => i.op === 14).words[0], 5348,
    `${what}: the addressing model must stay PhysicalStorageBuffer64`);
};
const assertStripped = (r, what) => {
  assert.ok(r.applied.includes(STRIP), `${what}: R8 must still strip (log: ${r.log.join(' | ')})`);
};

test('R8 census arm A: an Output pointer passed to a call declines the downgrade', () => {
  const r = run([
    ...PSB_BASE(), F32, VOID,
    ptrTy(21, 3, 20),                            // %21 = ptr Output f32
    varIns(21, 30, 3),                           // %30 = Output variable
    ...callee(25, 26, 27, 21),
    ...callerWith(28, 29, [call(35, 26, 30)]),
  ]);
  assertDeclined(r, 'arm A (Output call argument)');
});

test('R8 census arm A: a StorageBuffer pointer passed to a call declines without VariablePointers', () => {
  const r = run([
    ...PSB_BASE(), F32, VOID,
    ptrTy(21, 12, 20),                           // %21 = ptr StorageBuffer f32
    varIns(21, 30, 12),                          // %30 = StorageBuffer variable
    ...callee(25, 26, 27, 21),
    ...callerWith(28, 29, [call(35, 26, 30)]),
  ]);
  assertDeclined(r, 'arm A (StorageBuffer call argument, no VariablePointers)');
});

test('R8 census arm A exemptions: Function/UniformConstant/Workgroup/Private call arguments still strip', () => {
  for (const sc of [7, 0, 4, 6]) {
    const r = run([
      ...PSB_BASE(), F32, VOID,
      ptrTy(21, sc, 20),
      varIns(21, 30, sc),
      ...callee(25, 26, 27, 21),
      ...callerWith(28, 29, [call(35, 26, 30)]),
    ]);
    assertStripped(r, `arm A exemption (storage class ${sc} call argument)`);
  }
});

test('R8 census arm A exemption: a Function-class argument taken from the caller parameter still strips', () => {
  const r = run([
    ...PSB_BASE(), F32, VOID,
    ptrTy(21, 7, 20),                            // %21 = ptr Function f32
    ...callee(25, 26, 27, 21),
    { op: 33, words: [28, 23, 21] },             // OpTypeFunction void(ptr Function f32)
    { op: 54, words: [23, 29, 0, 28] },          // OpFunction %29
    { op: 55, words: [21, 31] },                 // OpFunctionParameter %31
    { op: 248, words: [91] },                    // OpLabel
    call(35, 26, 31),
    { op: 253, words: [] },                      // OpReturn
    { op: 56, words: [] },                       // OpFunctionEnd
  ]);
  assertStripped(r, 'arm A exemption (Function parameter forwarded to a call)');
});

test('R8 census arm A exemption: StorageBuffer call argument strips with either VariablePointers capability', () => {
  for (const vp of [4441, 4442]) {
    const r = run([
      cap(1), cap(5347), cap(vp),
      ext('SPV_KHR_physical_storage_buffer'),
      memModel(5348, 1),
      entryPoint(0, 1),
      F32, VOID,
      ptrTy(21, 12, 20),
      varIns(21, 30, 12),
      ...callee(25, 26, 27, 21),
      ...callerWith(28, 29, [call(35, 26, 30)]),
    ]);
    assertStripped(r, `arm A exemption (StorageBuffer argument with capability ${vp})`);
  }
});

test('R8 census arm A exemption: a declared-but-uncalled pointer-parameter function still strips', () => {
  const r = run([
    ...PSB_BASE(), F32, VOID,
    ptrTy(21, 3, 20),                            // %21 = ptr Output f32
    ...callee(25, 26, 27, 21),                   // declared, never called
  ]);
  assertStripped(r, 'arm A exemption (uncalled pointer-parameter function)');
});

test('R8 census arm B: a variable allocating a pointer type declines the downgrade', () => {
  const r = run([
    ...PSB_BASE(), F32, VOID,
    ptrTy(21, 6, 20),                            // %21 = ptr Private f32
    ptrTy(22, 6, 21),                            // %22 = ptr Private (ptr Private f32)
    varIns(22, 30, 6),                           // %30 allocates a pointer value
  ]);
  assertDeclined(r, 'arm B (pointer-holding variable)');
});

test('R8 census arm B control: a variable allocating a plain pointer still strips', () => {
  const r = run([
    ...PSB_BASE(), F32, VOID,
    ptrTy(21, 6, 20),                            // %21 = ptr Private f32
    varIns(21, 30, 6),                           // %30 allocates a float, via a pointer
  ]);
  assertStripped(r, 'arm B control (pointer-to-float variable)');
});

test('R8 census arm C: a pointer OpSelect declines without VariablePointers, strips with it', () => {
  const shape = (extraCaps) => [
    cap(1), cap(5347), ...extraCaps,
    ext('SPV_KHR_physical_storage_buffer'),
    memModel(5348, 1),
    entryPoint(0, 1),
    F32, VOID, BOOL,
    ptrTy(21, 6, 20),                            // %21 = ptr Private f32
    varIns(21, 30, 6), varIns(21, 31, 6),
    { op: 43, words: [24, 32, 1] },              // OpConstant %32 = true
    ...callerWith(28, 29, [
      { op: 169, words: [21, 35, 32, 30, 31] },  // OpSelect %35 = %32 ? %30 : %31
    ]),
  ];
  assertDeclined(run(shape([])), 'arm C (OpSelect over pointers, no VariablePointers)');
  assertStripped(run(shape([cap(4441)])), 'arm C exemption (OpSelect with VariablePointersStorageBuffer)');
});

test('R8 census arm C: a pointer OpPhi declines without VariablePointers', () => {
  const r = run([
    ...PSB_BASE(), F32, VOID,
    ptrTy(21, 6, 20),
    varIns(21, 30, 6), varIns(21, 31, 6),
    ...callerWith(28, 29, [
      { op: 249, words: [92] },                  // OpBranch -> %92
      { op: 248, words: [92] },                  // OpLabel %92
      { op: 245, words: [21, 35, 30, 91, 31, 91] }, // OpPhi %35 over the pointers
    ]),
  ]);
  assertDeclined(r, 'arm C (OpPhi over pointers, no VariablePointers)');
});

test('R8 census arm D: OpPtrEqual declines the downgrade', () => {
  const r = run([
    ...PSB_BASE(), F32, VOID, BOOL,
    ptrTy(21, 6, 20),
    varIns(21, 30, 6), varIns(21, 31, 6),
    ...callerWith(28, 29, [
      { op: 401, words: [24, 35, 30, 31] },      // OpPtrEqual %35 = (%30 == %31)
    ]),
  ]);
  assertDeclined(r, 'arm D (OpPtrEqual)');
});

// The assembled fixtures (spirv-as, validated as physical modules): the
// pre-fix pipeline downgraded all three into modules spirv-val rejects.
const loadR8 = (name) => new Uint8Array(fs.readFileSync(new URL(`../../../../testdata/spirv/r8/${name}`, import.meta.url)));
for (const [file, arm] of [
  ['r8-ptr-call-arg.spv', 'A'],
  ['r8-ptr-holding-var.spv', 'B'],
  ['r8-ptr-select.spv', 'C'],
]) {
  test(`R8 census fixture ${file} (arm ${arm}): downgrade declined, module stays physical`, () => {
    assertDeclined(applySpirvFix(loadR8(file)), `fixture ${file}`);
  });
}
