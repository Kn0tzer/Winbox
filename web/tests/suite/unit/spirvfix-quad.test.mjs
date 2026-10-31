// spirvfix R17 (quad-ops-to-shuffle): GroupNonUniformQuad lowers onto
// subgroup shuffles only for the constant, Subgroup-scope forms. These
// tests pin the rule's edges: the compiled fixtures lower; a broadcast
// whose index is not a constant is left for the audit to reject by name;
// an out-of-range swap direction is left; a stray declaration is stripped.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applySpirvFix, parseSpirv, serializeSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';

const fixture = (name) => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/subgroup/${name}.frag.spv`, import.meta.url)));
const capsOf = (bytes) => parseSpirv(bytes).insns.filter(i => i.op === 17).map(i => i.words[0]);

test('quad broadcast and swap fixtures are lowered, capability 68 dropped', () => {
  for (const name of ['quad', 'quadswap']) {
    const r = applySpirvFix(fixture(name));
    assert.ok(r.applied.includes('quad-ops-to-shuffle'), `${name}: ${JSON.stringify(r.applied)}`);
    const out = parseSpirv(r.bytes);
    assert.ok(!capsOf(r.bytes).includes(68), `${name}: capability 68 must be gone`);
    assert.ok(capsOf(r.bytes).includes(65), `${name}: GroupNonUniformShuffle must be declared`);
    assert.ok(!out.insns.some(i => i.op === 365 || i.op === 366), `${name}: no quad ops may remain`);
  }
  const swapOut = parseSpirv(applySpirvFix(fixture('quadswap')).bytes);
  assert.ok(swapOut.insns.some(i => i.op === 346), 'swap must become OpGroupNonUniformShuffleXor');
  const bcOut = parseSpirv(applySpirvFix(fixture('quad')).bytes);
  assert.ok(bcOut.insns.some(i => i.op === 345), 'broadcast must become OpGroupNonUniformShuffle');
});

test('a quad broadcast with a non-constant index is left untouched', () => {
  const mod = parseSpirv(fixture('quad'));
  const quad = mod.insns.find(i => i.op === 365);
  const load = mod.insns.find(i => i.op === 61);
  assert.ok(quad && load, 'fixture shape');
  quad.words[4] = load.resultId; // index is now a runtime value
  const r = applySpirvFix(serializeSpirv(mod));
  assert.ok(!r.applied.includes('quad-ops-to-shuffle'), JSON.stringify(r.applied));
  const out = parseSpirv(r.bytes);
  assert.ok(capsOf(r.bytes).includes(68), 'capability 68 must survive for the audit to reject by name');
  assert.ok(out.insns.some(i => i.op === 365), 'the quad op must survive');
});

test('a quad swap with direction 3 is left untouched', () => {
  const mod = parseSpirv(fixture('quadswap'));
  const swap = mod.insns.find(i => i.op === 366);
  const dirConst = mod.insns.find(i => i.resultId === swap.words[4]);
  assert.ok(dirConst && dirConst.op === 43, 'fixture shape');
  dirConst.words[2] = 3; // not a valid quad swap direction
  const r = applySpirvFix(serializeSpirv(mod));
  assert.ok(!r.applied.includes('quad-ops-to-shuffle'), JSON.stringify(r.applied));
  assert.ok(capsOf(r.bytes).includes(68));
});

test('a stray GroupNonUniformQuad declaration is stripped', () => {
  const mod = {
    header: { magic: 0x07230203, version: 0x00010600, generator: 0, bound: 100, schema: 0 },
    insns: [
      { op: 17, words: [1] },
      { op: 17, words: [68] },
      { op: 15, words: [4, 1, 0x6e69616d, 0] },
    ],
  };
  const r = applySpirvFix(serializeSpirv(mod));
  assert.ok(r.applied.includes('quad-ops-to-shuffle'), JSON.stringify(r.applied));
  assert.ok(!capsOf(r.bytes).includes(68), 'the stray capability must be gone');
});
