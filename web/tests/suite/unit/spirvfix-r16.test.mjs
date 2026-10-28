// web/tests/suite/unit/spirvfix-r16.test.mjs -- SPIR-V fix R16
// "strip-dead-undefs" regression tests (lane-psb).
//
// dxbc-spirv emits OpUndef for DXIL temp registers that end up never read.
// The real root12 UAV pixel shader (testdata/spirv/d3d12-ps-root-uav.spv)
// carries three dead OpUndef %uint in main, and naga 30.2.0's validator
// rejects the whole function over them ("Function [0] 'main' is invalid"
// -- proven against the vendored naga on this exact module: removing
// exactly the dead undefs makes it translate, spirv-val still passes).
// R16 removes a function-local OpUndef only when its result id has NO
// consumer anywhere in the module (a literal word equal to the id counts
// as a consumer: fail-closed, the module then stays untranslated rather
// than risk a mistranslation). Used undefs and global undefs are kept.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  applySpirvFix, parseSpirv, serializeSpirv, REWRITES,
} from '../../../../tools/spirvfix/spirvfix.mjs';

const uavRootBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-root-uav.spv', import.meta.url)));
const cbvBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-root-cbv.spv', import.meta.url)));
const srvRootBytes = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-ps-root-srv.spv', import.meta.url)));

function functionLocalUndefs(bytes) {
  const mod = parseSpirv(bytes);
  const ids = [];
  let inFunc = false;
  for (const ins of mod.insns) {
    if (ins.op === 54) inFunc = true;
    else if (ins.op === 56) inFunc = false;
    else if (inFunc && ins.op === 1) ids.push(ins.words[1]);
  }
  return ids;
}

test('R16: the root UAV module starts with exactly three dead OpUndef in main', () => {
  assert.deepEqual(functionLocalUndefs(uavRootBytes), [44, 45, 46]);
});

test('R16: dead undefs are stripped and the module still serves its root UAV', () => {
  const out = applySpirvFix(uavRootBytes, {});
  assert.ok(out.applied.includes('strip-dead-undefs'), 'applied: ' + out.applied.join(','));
  assert.deepEqual(functionLocalUndefs(out.bytes), []);
  // Composition: R15b still re-roots the (now undef-free) module to its
  // synthesized storage variable.
  const names = new Map();
  for (const ins of parseSpirv(out.bytes).insns) {
    if (ins.op === 5) { // OpName
      const bytes = [];
      for (let w = 1; w < ins.words.length; w++) { let v = ins.words[w]; for (let k = 0; k < 4; k++) { const c = v & 0xff; if (!c) break; bytes.push(c); v >>= 8; } }
      names.set(ins.words[0], String.fromCharCode(...bytes));
    }
  }
  assert.ok([...names.values()].includes('bw_root_p0'), 're-rooted variable present');
});

test('R16: modules without dead undefs are untouched by the rule', () => {
  for (const bytes of [cbvBytes, srvRootBytes]) {
    const out = applySpirvFix(bytes, {});
    assert.ok(!out.applied.includes('strip-dead-undefs'), 'applied: ' + out.applied.join(','));
  }
});

test('R16 fail-closed: an undef whose id appears as any other word is kept', () => {
  const mod = parseSpirv(uavRootBytes);
  // Wire id 44 in as an operand of the shader's index OpIAdd: the rule
  // cannot prove it dead (its test is purely structural), so 44 must
  // survive while the still-dead 45 and 46 are stripped.
  const iadd = mod.insns.find(i => i.op === 128);
  assert.ok(iadd, 'fixture has an OpIAdd');
  iadd.words[3] = 44;
  const out = applySpirvFix(serializeSpirv(mod), {});
  assert.ok(out.applied.includes('strip-dead-undefs'));
  assert.deepEqual(functionLocalUndefs(out.bytes), [44]);
});

test('R16: registered in the REWRITES list', () => {
  assert.ok(REWRITES.includes('strip-dead-undefs'));
});
