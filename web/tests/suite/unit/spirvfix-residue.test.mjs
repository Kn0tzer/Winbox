// spirvfix-residue.test.mjs -- guards for the two rawscan residue
// items closed by lane-spirvres (campaign section 4.42-on-branch):
//  Item 1: stripPointBuiltins' escape check missed a use of the
//  point-builtin pointer as an OpFunctionCall argument (and as an
//  OpSelect / OpPhi merge input): the pass stripped the variable
//  while the call still named it, producing a module with a
//  dangling id that only failed downstream.
//  Item 3: for a module with no debug instructions, the split
//  helper shared by R1 and R12 and r15SpliceAdditions spliced the
//  synthesized bw_* OpName at index 0 -- before OpCapability --
//  which spirv-val and naga reject ('Name cannot appear before
//  the memory model instruction').
// Fixtures: testdata/spirv/spirvres/ (.spvasm sources alongside;
// root-cbv-noname and texel-hud-noname are real corpus modules with
// every debug instruction removed).
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { applySpirvFix, parseSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../../web/vendor/naga/node.js';

await naga.init();

const DIR = new URL('../../../../testdata/spirv/spirvres/', import.meta.url);
const load = (name) => new Uint8Array(fs.readFileSync(new URL(name, DIR)));

const nameOf = (ins) => {
  const bytes = [];
  for (let k = 1; k < ins.words.length; k++) {
    const w = ins.words[k];
    for (const shift of [0, 8, 16, 24]) {
      const b = (w >>> shift) & 255;
      if (!b) return new TextDecoder().decode(new Uint8Array(bytes));
      bytes.push(b);
    }
  }
  return new TextDecoder().decode(new Uint8Array(bytes));
};

// The SPIR-V 2.4 layout invariant the splice fix restores: every
// OpName sits after the header block (OpMemoryModel 14, OpEntryPoint
// 15) and before the first annotation (OpDecorate 71 /
// OpMemberDecorate 72).
const assertNamesLegal = (mod) => {
  const mm = mod.insns.findIndex(i => i.op === 14);
  const ep = mod.insns.findIndex(i => i.op === 15);
  const firstAnnot = mod.insns.findIndex(i => i.op === 71 || i.op === 72);
  const names = mod.insns.map((i, ix) => i.op === 5 ? ix : -1).filter(x => x >= 0);
  assert.ok(names.length > 0, 'expected at least one synthesized OpName');
  for (const ix of names) {
    assert.ok(ix > mm && ix > ep,
      `OpName at ${ix} must follow the header (memory model ${mm}, entry point ${ep})`);
    if (firstAnnot >= 0) assert.ok(ix < firstAnnot,
      `OpName at ${ix} must precede annotations (${firstAnnot})`);
  }
  return names;
};

test('item 1: a point builtin passed as a call argument declines the strip', () => {
  const r = applySpirvFix(load('point-call-arg.spv'));
  assert.ok(!r.applied.includes('strip-point-builtins'), 'the strip must decline');
  assert.ok(r.log.some(l => l.includes('used outside direct loads/stores')),
    'the decline is the existing named one');
  const out = parseSpirv(r.bytes);
  const psDeco = out.insns.find(i => i.op === 71 && i.words[1] === 11 && i.words[2] === 1);
  assert.ok(psDeco, 'the PointSize decoration survives');
  const psId = psDeco.words[0];
  const varIns = out.insns.find(i => i.resultId === psId);
  assert.ok(varIns && varIns.op === 59, 'the PointSize variable survives');
  const call = out.insns.find(i => i.op === 57);
  assert.ok(call, 'the OpFunctionCall survives');
  assert.equal(call.words[3], psId, 'the call still passes the live variable');
});

test('item 1 control: direct-only point builtin uses still strip', () => {
  const r = applySpirvFix(load('point-direct.spv'));
  assert.ok(r.applied.includes('strip-point-builtins'));
  const out = parseSpirv(r.bytes);
  assert.ok(!out.insns.some(i => i.op === 71 && i.words[1] === 11 && i.words[2] === 1),
    'the PointSize decoration is gone');
  assert.equal(out.insns.filter(i => i.op === 59).length, 1,
    'only the position output variable remains');
  assert.doesNotThrow(() => naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes }));
});

test('item 3: name-less combined module (R1) places synthesized names legally', () => {
  const r = applySpirvFix(load('combined-noname.spv'));
  assert.ok(r.applied.includes('split-combined-sampler'));
  const out = parseSpirv(r.bytes);
  const names = assertNamesLegal(out);
  assert.ok(names.some(ix => nameOf(out.insns[ix]).startsWith('bw_combined_')));
  assert.doesNotThrow(() => naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes }));
});

test('item 3: name-less combined module with no surviving annotations (R1)', () => {
  const r = applySpirvFix(load('combined-noname-noannot.spv'));
  assert.ok(r.applied.includes('split-combined-sampler'));
  const out = parseSpirv(r.bytes);
  assertNamesLegal(out);
  const firstAnnot = out.insns.findIndex(i => i.op === 71 || i.op === 72);
  const ep = out.insns.findIndex(i => i.op === 15);
  assert.ok(firstAnnot > ep, 'synthesized annotations also land after the header');
  // No naga assertion here: naga rejects a Location-less fragment
  // output at its own interface validation ('Entry point main at
  // Fragment is invalid'), a rule orthogonal to section layout.
  // Layout validity for this shape is the position assertions above
  // plus spirv-val on the produced module (lane-verified).
});

test('item 3: name-less texel-buffer module (R12) places synthesized names legally', () => {
  const r = applySpirvFix(load('texel-hud-noname.spv'));
  assert.ok(r.applied.includes('texel-buffer-to-2d'));
  const out = parseSpirv(r.bytes);
  assertNamesLegal(out);
  assert.doesNotThrow(() => naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes }));
});

test('item 3: name-less root-descriptor module (R15) places synthesized names legally', () => {
  const r = applySpirvFix(load('root-cbv-noname.spv'));
  assert.ok(r.applied.includes('root-descriptor-bda'));
  const out = parseSpirv(r.bytes);
  const names = assertNamesLegal(out);
  assert.ok(names.some(ix => nameOf(out.insns[ix]).startsWith('bw_root_')));
  assert.doesNotThrow(() => naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes }));
});
