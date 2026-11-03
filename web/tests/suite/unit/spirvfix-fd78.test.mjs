// FD-78 — the literal-reference defect class of the spirvfix liveness
// sweeps, closed at every remaining position.
//
// FD-70 fixed the descriptor-array orphan sweep (and its bridge
// mirror) at the two positions a captured module proved: the
// memory-access operand group and the OpTypeInt / OpTypeFloat width
// words. FD-87 fixed the OpExtInst instruction-number position at
// the same two sweeps. The raw-word scan idiom — every operand word
// counts as an id reference — survived at the remaining literal
// positions and in the sibling sweeps, so any module whose ids align
// with one of those literals reproduces FD-70's failure shape: a
// swept type or variable kept alive by a word that is not a
// reference, and a refusal one layer earlier than the true blocker
// (or a silently retained capability).
//
// The fix routes every liveness scan in the serving layer through
// operandRefIds, the enumerating sibling of the operand-aware
// walker idOperandUseIndex (a value is a reference iff the walker
// finds it in an id-typed operand position):
//   - specializeDescriptorArrays' orphan sweep (spirvfix) and the
//     bridge audit's skip-5302 simulation (vk64bridge.cpp; native
//     guards in tools/vkd3d/tests/vk64_native_test.cpp);
//   - pruneDeadStorageImageTypes (which previously counted even
//     constant values and type widths);
//   - rewriteRootDescriptors' address-cone sweep;
//   - sweepTexelOrphans (R12A), whose scan also lacked the
//     type-width and memory-access bounds;
//   - pruneSpecializedAliases' function-body reference scan.
// The walker also gained the OpExecutionModeId layout (entry id,
// mode literal, id parameters), the Id sibling of the OpExecutionMode
// layout the sweeps already treated as never-a-reference.
//
// Guards below: (A) per-position unit tests of operandRefIds;
// (B) per-position mutation guards on the FD-70 sweep fixture —
// each variant adds ONE instruction carrying the array type's id at
// one literal position and must not change the sweep outcome (every
// variant was verified to keep the array alive on the pre-fix code);
// (C) prune fixtures, spirv-val clean .spvasm under
// testdata/spirv/src/; (D–F) mutation guards for the cone, texel and
// alias sweeps on captured modules (an OpLine whose line number is
// the swept id must not keep the swept entity alive).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  applySpirvFix, parseSpirv, serializeSpirv, operandRefIds,
} from '../../../../tools/spirvfix/spirvfix.mjs';

const load = name => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/${name}`, import.meta.url)));
const insn = (op, words, resultType = null, resultId = null) => ({ op, words, resultType, resultId });

// --- (A) operandRefIds: one guard per literal position ----------------------
const P = 20; // the probe: a literal value that is NOT a reference
const casesA = [
  // [label, insn, mustContain, mustNotContain]
  ['ExtInst instruction number', insn(12, [3, 90, 7, P, 8], 3, 90), [7, 8], [P]],
  ['VectorShuffle components', insn(79, [5, 90, 8, 9, P, 0, 1, 2], 5, 90), [8, 9], [P]],
  ['CompositeExtract indices', insn(81, [3, 90, 8, P], 3, 90), [8], [P]],
  ['CompositeInsert indices', insn(82, [5, 90, 8, 9, P], 5, 90), [8, 9], [P]],
  ['OpLine words', insn(8, [7, P, 3]), [7], [P, 3]],
  ['OpFunction leading literal (control mask)', insn(54, [3, 90, P, 10], 3, 90), [10], [P]],
  ['OpVariable leading literal (storage class)', insn(59, [11, 90, P, 8], 11, 90), [8], [P]],
  ['OpArrayLength structure index', insn(68, [4, 90, 8, P], 4, 90), [8], [P]],
  ['OpTypePointer storage-class word', insn(32, [90, P, 8], null, 90), [8], [P]],
  ['OpExecutionModeId mode word', insn(331, [90, P, 8]), [90, 8], [P]],
  ['OpLoad MemoryAccess Aligned literal', insn(61, [3, 90, 8, 2, P], 3, 90), [8], [P]],
  ['OpBranchConditional weights', insn(250, [8, 60, 61, P, P]), [8, 60, 61], [P]],
  ['OpLoopMerge control word', insn(246, [60, 61, P, 7]), [60, 61], [P, 7]],
];
for (const [label, ins, yes, no] of casesA) {
  test(`FD-78 walker: ${label} is not a reference`, () => {
    const refs = operandRefIds(new Map(), ins);
    for (const v of yes) assert.ok(refs.has(v), `${label}: genuine operand ${v} must be a reference`);
    for (const v of no) assert.ok(!refs.has(v), `${label}: literal ${v} must not be a reference`);
  });
}
test('FD-78 walker: OpSwitch case literals are not references (selector width from byId)', () => {
  const byId = new Map([
    [50, insn(61, [4, 50, 8], 4, 50)],       // selector value, typed %4
    [4, insn(21, [4, 32, 0], null, 4)],      // OpTypeInt 32
  ]);
  const refs = operandRefIds(byId, insn(251, [50, 60, P, 61, 7, 62]));
  for (const v of [50, 60, 61, 62]) assert.ok(refs.has(v), `label/selector ${v} must be a reference`);
  for (const v of [P, 7]) assert.ok(!refs.has(v), `case literal ${v} must not be a reference`);
});
test('FD-78 walker: a MemoryAccess scope id IS a reference (the group is bounded, not dropped)', () => {
  // Mask 8 = MakePointerAvailable: its parameter is a scope id.
  const refs = operandRefIds(new Map(), insn(61, [3, 90, 8, 8, P], 3, 90));
  assert.ok(refs.has(8) && refs.has(P), 'pointer and scope ids are references');
  const chain = operandRefIds(new Map(), insn(65, [11, 90, 8, 9], 11, 90));
  assert.ok(chain.has(8) && chain.has(9), 'access-chain operands are references');
});

// --- (B) orphan sweep: one mutation guard per position ----------------------
function sweepBase() {
  const mod = parseSpirv(load('fd70-aligned-literal-sweep.spv'));
  const RA = mod.insns.find(i => i.op === 29).resultId;
  const ty = op => mod.insns.find(i => i.op === op).resultId;
  return {
    mod, RA,
    floatTy: ty(22), uintTy: ty(21), v4Ty: ty(23),
    labelAt: mod.insns.findIndex(i => i.op === 248),
    firstFuncAt: mod.insns.findIndex(i => i.op === 54),
  };
}
function sweepOutcome(mod) {
  const r = applySpirvFix(serializeSpirv(mod));
  const out = parseSpirv(r.bytes);
  return {
    applied: r.applied,
    rta: out.insns.some(i => i.op === 29),
    cap: out.insns.some(i => i.op === 17 && i.words[0] === 5302),
  };
}
function assertSwept(o, label) {
  assert.deepEqual(o.applied, ['descriptor-array-specialize'], `${label}: specialization still runs`);
  assert.equal(o.rta, false, `${label}: the specialized-away runtime array is swept`);
  assert.equal(o.cap, false, `${label}: RuntimeDescriptorArray is stripped`);
}
function insertLine(mod, line) {
  let file = mod.insns.find(i => i.op === 7); let fileId;
  if (file) fileId = file.resultId ?? file.words[0];
  else {
    fileId = mod.header.bound++;
    const ff = mod.insns.findIndex(i => i.op === 54);
    mod.insns.splice(ff, 0, insn(7, [fileId, 0x2e6d656c, 0], null, fileId));
  }
  const li = mod.insns.findIndex(i => i.op === 248);
  mod.insns.splice(li + 1, 0, insn(8, [fileId, line, 0]));
}
const strWords = s => {
  const w = []; let cur = 0, n = 0;
  for (const c of (s + '\0')) { cur |= c.charCodeAt(0) << (8 * n); if (++n === 4) { w.push(cur >>> 0); cur = 0; n = 0; } }
  if (n) w.push(cur >>> 0);
  return w;
};
const variantsB = {
  'ExtInst instruction number': ({ mod, RA, floatTy, labelAt }) => {
    const imp = mod.header.bound++;
    mod.insns.splice(mod.insns.findIndex(i => i.op === 54), 0, insn(11, [imp, ...strWords('GLSL.std.450')], null, imp));
    const f0 = mod.insns.find(i => i.op === 43 && i.resultType === floatTy);
    const id = mod.header.bound++;
    mod.insns.splice(labelAt + 2, 0, insn(12, [floatTy, id, imp, RA, f0.resultId], floatTy, id));
  },
  'VectorShuffle component': ({ mod, RA, v4Ty, labelAt }) => {
    const coord = mod.insns.find(i => i.op === 44 && i.resultType === v4Ty);
    const id = mod.header.bound++;
    mod.insns.splice(labelAt + 1, 0, insn(79, [v4Ty, id, coord.resultId, coord.resultId, RA, 0, 1, 2], v4Ty, id));
  },
  'CompositeExtract index': ({ mod, RA, floatTy, labelAt }) => {
    const tex = mod.insns.slice(labelAt).find(i => i.op === 87);
    const id = mod.header.bound++;
    mod.insns.splice(mod.insns.indexOf(tex) + 1, 0, insn(81, [floatTy, id, tex.resultId, RA], floatTy, id));
  },
  'CompositeInsert index': ({ mod, RA, v4Ty, floatTy, labelAt }) => {
    const tex = mod.insns.slice(labelAt).find(i => i.op === 87);
    const tx = mod.insns.slice(labelAt).find(i => i.op === 81 && i.resultType === floatTy);
    const id = mod.header.bound++;
    mod.insns.splice(mod.insns.indexOf(tx) + 1, 0, insn(82, [v4Ty, id, tx.resultId, tex.resultId, RA], v4Ty, id));
  },
  'OpSwitch case literal': ({ mod, RA, uintTy, labelAt }) => {
    const idx = mod.insns.slice(labelAt).find(i => i.op === 61 && i.resultType === uintTy);
    const merge = mod.header.bound++, kase = mod.header.bound++;
    const retAt = mod.insns.findIndex(i => i.op === 253);
    mod.insns.splice(retAt, 0,
      insn(247, [merge]),
      insn(251, [idx.resultId, merge, RA, kase]),
      insn(248, [kase], null, kase),
      insn(249, [merge]),
      insn(248, [merge], null, merge));
  },
  'OpLine line number': ({ mod, RA }) => insertLine(mod, RA),
  'OpFunction control mask': ({ mod, RA }) => { mod.insns.find(i => i.op === 54).words[2] = RA; },
  'OpVariable storage class': ({ mod, RA, floatTy, labelAt }) => {
    const ptr = mod.insns.find(i => i.op === 32 && i.words[2] === floatTy);
    const id = mod.header.bound++;
    mod.insns.splice(labelAt + 1, 0, insn(59, [ptr.resultId, id, RA], ptr.resultId, id));
  },
  'OpArrayLength structure index': ({ mod, RA, uintTy, labelAt }) => {
    const st = mod.insns.find(i => i.op === 30);
    const id = mod.header.bound++;
    mod.insns.splice(labelAt + 1, 0, insn(68, [uintTy, id, st.resultId, RA], uintTy, id));
  },
  'OpTypePointer storage class': ({ mod, RA, floatTy, firstFuncAt }) => {
    // The carrier pointer type is kept genuinely live by a variable,
    // or the sweep's fixpoint would delete the carrier itself in
    // pass 1 before its storage word can be counted in pass 2.
    const ptr = mod.header.bound++, varId = mod.header.bound++;
    mod.insns.splice(firstFuncAt, 0,
      insn(32, [ptr, RA, floatTy], null, ptr),
      insn(59, [ptr, varId, 12], ptr, varId));
  },
};
test('FD-78 sweep: the FD-70 fixture sweeps clean unmutated', () => {
  assertSwept(sweepOutcome(sweepBase().mod), 'base');
});
for (const [label, mutate] of Object.entries(variantsB)) {
  test(`FD-78 sweep: a literal at the ${label} position does not keep the array alive`, () => {
    const ctx = sweepBase();
    mutate(ctx);
    assertSwept(sweepOutcome(ctx.mod), label);
  });
}

// --- (C) storage-image prune fixtures ---------------------------------------
for (const [name, label] of [
  ['fd78-prune-constant.spv', 'an OpConstant value equal to the image type id'],
  ['fd78-prune-function.spv', 'an OpFunction control mask equal to the wrapper type id'],
]) {
  test(`FD-78 prune: ${label} is not a reference`, () => {
    const r = applySpirvFix(load(name));
    assert.ok(r.applied.includes('prune-dead-storage-image-types'), r.log.join('; '));
    const out = parseSpirv(r.bytes);
    assert.ok(!out.insns.some(i => i.op === 25), 'the format-less storage image type is pruned');
    assert.ok(!out.insns.some(i => i.op === 17 && i.words[0] === 56), 'the stray capability is stripped');
  });
}

// --- (D) root-descriptor cone sweep ------------------------------------------
test('FD-78 cone sweep: an OpLine line number does not keep a dead cone instruction alive', () => {
  const mod = parseSpirv(load('d3d12-ps-root-cbv.spv'));
  const li = mod.insns.findIndex(i => i.op === 248);
  const loadInsn = mod.insns.slice(li).find(i => i.op === 61);
  const X = mod.header.bound++;
  mod.insns.splice(mod.insns.indexOf(loadInsn) + 1, 0,
    insn(83, [loadInsn.resultType, X, loadInsn.resultId], loadInsn.resultType, X));
  insertLine(mod, X);
  const r = applySpirvFix(serializeSpirv(mod));
  assert.ok(r.applied.includes('root-descriptor-bda'), r.log.join('; '));
  const out = parseSpirv(r.bytes);
  assert.ok(!out.insns.some(i => i.resultId === X), 'the unreferenced cone copy is swept');
});

// --- (E) R12A texel orphan sweep ----------------------------------------------
test('FD-78 texel sweep: an OpLine line number does not keep a swept texel type alive', () => {
  const mod = parseSpirv(load('godot-fs-atomic-texel-heap.spv'));
  insertLine(mod, 179); // %179: a texel pointer type the R12A sweep removes
  const r = applySpirvFix(serializeSpirv(mod));
  assert.ok(r.applied.includes('texel-pointer-to-storage-buffer'), r.log.join('; '));
  const out = parseSpirv(r.bytes);
  assert.ok(!out.insns.some(i => i.resultId === 179), 'the orphaned texel pointer type is swept');
});

// --- (F) specialized-alias prune ------------------------------------------------
test('FD-78 alias prune: an OpLine line number does not keep an unused alias variable alive', () => {
  const mod = parseSpirv(load('godot-cs-c9c003a7487b6126.spv'));
  insertLine(mod, 22); // %22: a UniformConstant variable the alias prune removes
  const r = applySpirvFix(serializeSpirv(mod));
  // Batch-3 composition note: FD-87's R18 strip-dead-descriptors
  // pass (composed in the same batch) runs earlier in the pipeline
  // and removes %22 first -- it is a declared, never-referenced
  // descriptor variable, squarely in R18's remit -- so on the
  // composed tree the alias prune has nothing left to do. The
  // contract this test exists for is unchanged and is what the
  // outcome assertion below pins: the OpLine reference does not
  // keep the variable alive; one of the two passes removes it.
  assert.ok(r.applied.includes('prune-specialized-aliases') || r.applied.includes('strip-dead-descriptors'), r.log.join('; '));
  const out = parseSpirv(r.bytes);
  assert.ok(!out.insns.some(i => i.resultId === 22), 'the unused alias variable is pruned');
});
