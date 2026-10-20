// web/tests/spirvfix.test.mjs — SPIR-V pre-pass corpus gate.
//
// Covers tools/spirvfix/spirvfix.mjs through the same toWGSL('spirv') entry
// point web/vkwebgpu.mjs uses: every testdata/spirv/*.spv module must convert
// through the vendored naga (30.2.0) after the pre-pass.
//
// Corpus provenance:
//   p1-vkcube.* — byte-identical extract of vkfix_{vert,frag}_spv from
//     tools/rootfs64/libvk64/vkfixture_spirv.h (vkcube's own cube.vert /
//     cube.frag, vulkan-tools 1.3.275, glslangValidator, SPIR-V 1.0).
//   p2-p8 — rebuilt byte-identical from testdata/spirv/src/*.vert|*.frag via
//     `glslangValidator -V`; DXVK-typical features: combined samplers,
//     implicit/explicit LOD, specialization constants, separate image+sampler,
//     push constants, multi-texture, shadow (Dref) sampling, UBO lighting.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { applySpirvFix, parseSpirv, serializeSpirv, REWRITES } from '../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../vendor/naga/node.js';
import { toWGSL } from '../shader.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const corpusDir = join(repoRoot, 'testdata/spirv');
const modules = readdirSync(corpusDir).filter(f => f.endsWith('.spv')).sort();
const bytesOf = f => new Uint8Array(readFileSync(join(corpusDir, f)));
const stageOf = f => (f.includes('.vert.') ? 'vertex' : 'fragment');

// Expected rewrites per module, from tools/spirvfix/gate.mjs.
const EXPECTED = {
  'p1-vkcube.frag.spv': ['split-combined-sampler'],
  'p4-speconst.vert.spv': ['fold-spec-constants'],
  'p4-speconst.frag.spv': ['split-combined-sampler', 'fold-spec-constants'],
  'p5-pushconst.frag.spv': ['split-combined-sampler'],
  'p6-multitex.frag.spv': ['split-combined-sampler'],
  'p7-shadow.frag.spv': ['split-combined-sampler'],
  'p8-lighting.frag.spv': ['split-combined-sampler'],
};

// Without the pre-pass, naga rejects the combined-sampler fragment shaders:
// lookup_sampled_image has a single writer (parse_image_couple), reachable
// only from OpSampledImage, so a straight OpLoad of a combined uniform into
// OpImageSample* dies with InvalidId(<the load id>).
test('raw vkcube fragment fails without the pre-pass (InvalidId root cause)', () => {
  const raw = bytesOf('p1-vkcube.frag.spv');
  assert.throws(
    () => naga.translate({ from: 'spirv', to: 'wgsl', source: raw }),
    /InvalidId\(40\)/,
  );
  const fixed = applySpirvFix(raw);
  assert.deepEqual(fixed.applied, ['split-combined-sampler']);
  assert.doesNotThrow(() => naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes }));
});

test('every corpus module converts through toWGSL(spirv) with the pre-pass', async () => {
  assert.ok(modules.length >= 16, `expected >= 16 corpus modules, got ${modules.length}`);
  const seen = new Set();
  for (const f of modules) {
    const { applied } = applySpirvFix(bytesOf(f));
    for (const a of applied) {
      assert.ok(REWRITES.includes(a), `${f}: unknown rewrite ${a}`);
      seen.add(a);
    }
    assert.deepEqual(applied, EXPECTED[f] ?? [], `${f}: unexpected rewrite set`);
    const wgsl = await toWGSL('spirv', bytesOf(f), stageOf(f));
    assert.ok(typeof wgsl === 'string' && wgsl.length > 0, `${f}: empty WGSL`);
  }
  assert.deepEqual([...seen].sort(), ['fold-spec-constants', 'split-combined-sampler']);
});

test('pre-pass is idempotent; clean modules pass through byte-identical', () => {
  for (const f of modules) {
    const raw = bytesOf(f);
    const once = applySpirvFix(raw);
    const twice = applySpirvFix(once.bytes);
    assert.deepEqual(twice.applied, [], `${f}: second run must apply nothing`);
    assert.deepEqual([...twice.bytes], [...once.bytes], `${f}: second run byte-identical`);
    if (!once.applied.length)
      assert.deepEqual([...once.bytes], [...raw], `${f}: passthrough byte-identical`);
  }
});

test('spec-constant folds keep default values (p4 IEqual folds true)', () => {
  const raw = bytesOf('p4-speconst.frag.spv');
  const specOp = parseSpirv(raw).insns.find(i => i.op === 52); // OpSpecConstantOp
  assert.ok(specOp, 'p4 has a SpecConstantOp');
  const folded = parseSpirv(applySpirvFix(raw).bytes).insns
    .find(i => i.resultId === specOp.resultId);
  // IEqual(MODE, 0) with the default MODE=0 must fold to OpConstantTrue (41).
  assert.equal(folded.op, 41, 'folded to OpConstantTrue');
});

// Minimal hand-assembled module exercising the vector SpecConstantOp fold:
//   %vec  = OpSpecConstantOp %v2float FMul %sc0 %sc1   (1.5 * 2.5 per lane)
//   %wide = OpSpecConstantComposite %v4float %vec %vec
function buildVecFoldModule() {
  const words = [];
  const put = (op, ...ops) => words.push((((ops.length + 1) << 16) | op) >>> 0, ...ops.map(w => w >>> 0));
  const f32bits = x => new Uint32Array(new Float32Array([x]).buffer)[0];
  const str = s => {
    const b = [...s].map(c => c.charCodeAt(0)); b.push(0);
    while (b.length % 4) b.push(0);
    const o = [];
    for (let i = 0; i < b.length; i += 4)
      o.push((b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)) >>> 0);
    return o;
  };
  put(17, 1);                        // OpCapability Shader
  put(14, 0, 1);                     // OpMemoryModel Logical GLSL450
  put(15, 4, 12, ...str('main'), 7); // OpEntryPoint Fragment %12 "main" %7
  put(16, 12, 7);                    // OpExecutionMode %12 OriginUpperLeft
  put(71, 7, 30, 0);                 // OpDecorate %7 Location 0
  put(71, 8, 1, 1);                  // OpDecorate %8 SpecId 1
  put(71, 9, 1, 2);                  // OpDecorate %9 SpecId 2
  put(19, 1);                        // %1 = OpTypeVoid
  put(22, 2, 32);                    // %2 = OpTypeFloat 32
  put(23, 3, 2, 2);                  // %3 = OpTypeVector %2 2
  put(33, 4, 1);                     // %4 = OpTypeFunction %1
  put(23, 5, 2, 4);                  // %5 = OpTypeVector %2 4
  put(32, 6, 3, 5);                  // %6 = OpTypePointer Output %5
  put(59, 6, 7, 3);                  // %7 = OpVariable %6 Output
  put(50, 2, 8, f32bits(1.5));       // %8 = OpSpecConstant %2 1.5
  put(50, 2, 9, f32bits(2.5));       // %9 = OpSpecConstant %2 2.5
  put(52, 3, 10, 133, 8, 9);         // %10 = OpSpecConstantOp %3 FMul %8 %9
  put(51, 5, 11, 10, 10);            // %11 = OpSpecConstantComposite %5 %10 %10
  put(54, 1, 12, 0, 4);              // %12 = OpFunction %1 None %4
  put(248, 13);                      // %13 = OpLabel
  put(62, 7, 11);                    // OpStore %7 %11
  put(253);                          // OpReturn
  put(56);                           // OpFunctionEnd
  const out = new Uint32Array(5 + words.length);
  out.set([0x07230203, 0x00010000, 0, 14, 0], 0);
  out.set(words, 5);
  return new Uint8Array(out.buffer);
}

test('vector SpecConstantOp folds to scalar lanes + flattened composite', () => {
  const raw = buildVecFoldModule();
  const { bytes, applied } = applySpirvFix(raw);
  assert.deepEqual(applied, ['fold-spec-constants']);
  const mod = parseSpirv(bytes);
  assert.ok(!mod.insns.some(i => i.op === 50 || i.op === 51 || i.op === 52),
    'no SpecConstant* instructions remain');
  const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: bytes }));
  assert.ok(wgsl.includes('3.75'), 'folded value is 1.5*2.5 in every lane');
});

function buildDrawParametersModule() {
  const words = [];
  const put = (op, ...ops) => words.push((((ops.length + 1) << 16) | op) >>> 0, ...ops.map(w => w >>> 0));
  const str = s => {
    const b = [...s].map(c => c.charCodeAt(0)); b.push(0);
    while (b.length % 4) b.push(0);
    const out = [];
    for (let i = 0; i < b.length; i += 4)
      out.push((b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)) >>> 0);
    return out;
  };
  put(17, 1);                              // OpCapability Shader
  put(17, 4427);                           // OpCapability DrawParameters
  put(14, 0, 1);                           // OpMemoryModel Logical GLSL450
  put(15, 0, 13, ...str('main'), 7, 8, 10); // Vertex entry: BaseVertex, VertexIndex, Position
  put(71, 7, 11, 4424);                    // BaseVertex
  put(71, 8, 11, 42);                      // VertexIndex
  put(71, 10, 11, 0);                      // Position
  put(19, 1);                              // %1 = OpTypeVoid
  put(21, 2, 32, 1);                       // %2 = OpTypeInt 32 signed
  put(22, 3, 32);                          // %3 = OpTypeFloat 32
  put(23, 4, 3, 4);                        // %4 = OpTypeVector %3 4
  put(33, 5, 1);                           // %5 = OpTypeFunction %1
  put(32, 6, 1, 2);                        // %6 = OpTypePointer Input %2
  put(32, 9, 3, 4);                        // %9 = OpTypePointer Output %4
  put(43, 3, 11, 0);                       // %11 = OpConstant %3 0
  put(44, 4, 12, 11, 11, 11, 11);          // %12 = OpConstantComposite %4 zero
  put(59, 6, 7, 1);                        // %7 = OpVariable Input
  put(59, 6, 8, 1);                        // %8 = OpVariable Input
  put(59, 9, 10, 3);                       // %10 = OpVariable Output
  put(54, 1, 13, 0, 5);                    // %13 = OpFunction
  put(248, 14);                            // %14 = OpLabel
  put(61, 2, 15, 7);                       // %15 = OpLoad BaseVertex
  put(61, 2, 16, 8);                       // %16 = OpLoad VertexIndex
  put(130, 2, 17, 16, 15);                 // %17 = OpISub VertexIndex - BaseVertex
  put(62, 10, 12);                         // OpStore Position
  put(253);                                // OpReturn
  put(56);                                 // OpFunctionEnd
  const out = new Uint32Array(5 + words.length);
  out.set([0x07230203, 0x00010000, 0, 18, 0], 0);
  out.set(words, 5);
  return new Uint8Array(out.buffer);
}

test('DrawParameters base builtins become zero while VertexIndex stays a builtin load', () => {
  const raw = buildDrawParametersModule();
  const fixed = applySpirvFix(raw);
  const mod = parseSpirv(fixed.bytes);
  assert.equal(mod.insns.some(i => i.op === 17 && i.words[0] === 4427), false);
  assert.equal(mod.insns.some(i => i.op === 71 && i.words[1] === 11 && i.words[2] === 4424), false);
  const loads = mod.insns.filter(i => i.op === 61);
  assert.equal(loads.length, 1, 'the VertexIndex load remains');
  assert.equal(loads[0].words[2], 8);
  assert.ok(mod.insns.some(i => i.op === 83 && i.words[1] === 15), 'BaseVertex value is copied from zero');
  const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes }));
  assert.match(wgsl, /@builtin\(vertex_index\)/);
});

function buildMidDiscardBeforeSampleModule(discardOpcode = 252) {
  const mod = parseSpirv(bytesOf('p1-vkcube.frag.spv'));
  let next = mod.header.bound;
  const boolType = next++, condition = next++, killLabel = next++, mergeLabel = next++, helperResult = next++;
  mod.header.bound = next;
  const make = (op, words, resultType = null, resultId = null) => ({ op, words, resultType, resultId });
  const stringWords = value => {
    const bytes = [...value].map(c => c.charCodeAt(0)); bytes.push(0);
    while (bytes.length % 4) bytes.push(0);
    const words = [];
    for (let i = 0; i < bytes.length; i += 4)
      words.push((bytes[i] | (bytes[i + 1] << 8) | (bytes[i + 2] << 16) | (bytes[i + 3] << 24)) >>> 0);
    return words;
  };
  const lastType = mod.insns.findIndex(i => [41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 52, 59, 54].includes(i.op));
  mod.insns.splice(lastType < 0 ? mod.insns.length : lastType, 0,
    make(20, [boolType], null, boolType));                    // OpTypeBool
  let firstVariable = mod.insns.findIndex(i => i.op === 59 || i.op === 54);
  mod.insns.splice(firstVariable, 0,
    make(41, [boolType, condition], boolType, condition));    // OpConstantTrue
  const capabilities = mod.insns.filter(i => i.op === 17);
  const capAt = mod.insns.indexOf(capabilities.at(-1)) + 1;
  mod.insns.splice(capAt, 0, make(17, [5379]));               // DemoteToHelperInvocation
  const extension = make(10, stringWords('SPV_EXT_demote_to_helper_invocation'));
  mod.insns.splice(capAt + 1, 0, extension);
  const sampledType = mod.insns.find(i => i.op === 27).resultId;
  const firstSampleLoad = mod.insns.find(i => i.op === 61 && i.resultType === sampledType);
  const at = mod.insns.indexOf(firstSampleLoad);
  mod.insns.splice(at, 0,
    make(247, [mergeLabel, 0]),                          // OpSelectionMerge
    make(250, [condition, killLabel, mergeLabel]),       // if (true) discard
    make(248, [killLabel], null, killLabel),
    make(discardOpcode, []),
    ...(discardOpcode === 5380 ? [make(249, [mergeLabel])] : []), // Demote continues in the helper block
    make(248, [mergeLabel], null, mergeLabel),
    make(5381, [boolType, helperResult], boolType, helperResult)); // OpIsHelperInvocationEXT
  return serializeSpirv(mod);
}

test('mid-shader discard, demote and terminate in an if continue to sampling and validate', () => {
  for (const discardOpcode of [252, 5380, 4416]) {
    const fixed = applySpirvFix(buildMidDiscardBeforeSampleModule(discardOpcode));
    assert.ok(fixed.applied.includes('lower-mid-shader-discard'));
    const mod = parseSpirv(fixed.bytes);
    assert.equal(mod.insns.some(i => i.op === 5380 || i.op === 5381 || i.op === 4416), false,
      `discard opcode ${discardOpcode} was lowered`);
    assert.ok(mod.insns.some(i => i.op === 87), 'texture sample remains after the discard branch');
    const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes }));
    assert.match(wgsl, /textureSample/);
    assert.doesNotThrow(() => naga.validate(naga.parseWgsl(wgsl)));
  }
});

function buildNonEntryKillBeforeSampleModule() {
  const mod = parseSpirv(bytesOf('p1-vkcube.frag.spv'));
  const voidType = mod.insns.find(i => i.op === 19).resultId;
  const functionType = mod.insns.find(i => i.op === 33 && i.words[1] === voidType).resultId;
  const helperFunction = mod.header.bound++, helperLabel = mod.header.bound++, callResult = mod.header.bound++;
  const sampledType = mod.insns.find(i => i.op === 27).resultId;
  const firstSampleLoad = mod.insns.find(i => i.op === 61 && i.resultType === sampledType);
  mod.insns.splice(mod.insns.indexOf(firstSampleLoad), 0, {
    op: 57, words: [voidType, callResult, helperFunction], resultType: voidType, resultId: callResult,
  });
  mod.insns.push(
    { op: 54, words: [voidType, helperFunction, 0, functionType], resultType: voidType, resultId: helperFunction },
    { op: 248, words: [helperLabel], resultType: null, resultId: helperLabel },
    { op: 252, words: [], resultType: null, resultId: null },
    { op: 56, words: [], resultType: null, resultId: null },
  );
  return serializeSpirv(mod);
}

test('kill in a non-entry function sets private state and defers kill to entry return', () => {
  const fixed = applySpirvFix(buildNonEntryKillBeforeSampleModule());
  assert.ok(fixed.applied.includes('lower-mid-shader-discard'));
  const mod = parseSpirv(fixed.bytes);
  const entry = mod.insns.find(i => i.op === 15 && i.words[0] === 4).words[1];
  const helper = mod.insns.find(i => i.op === 54 && i.words[1] !== entry).words[1];
  let fn = null, helperHasKill = false;
  for (const ins of mod.insns) {
    if (ins.op === 54) fn = ins.words[1];
    else if (ins.op === 56) fn = null;
    else if (fn === helper && ins.op === 252) helperHasKill = true;
  }
  assert.equal(helperHasKill, false, 'helper returns to the entry function after setting the flag');
  assert.ok(mod.insns.some(i => i.op === 59 && i.words[2] === 6), 'private helper/discard state exists');
  const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes }));
  assert.match(wgsl, /textureSample/);
  assert.doesNotThrow(() => naga.validate(naga.parseWgsl(wgsl)));
});

test('toWGSL spirv input validation is unchanged', async () => {
  await assert.rejects(toWGSL('spirv', new Uint8Array(16)), /Invalid SPIR-V byte length/);
  await assert.rejects(toWGSL('spirv', new Uint8Array(20)), /Invalid SPIR-V magic/);
});
