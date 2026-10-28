// web/tests/suite/unit/vk-wgsl-r15-names.test.mjs — R15 WGSL name resolution.
// Naga's WGSL backend appends a trailing underscore to spirvfix R15
// generated globals (bw_root_p0 -> bw_root_p0_, bw_arr_g5_b0_p0_a0 ->
// bw_arr_g5_b0_p0_a0_), so scanWgslResources' exact-match regexes silently
// left rootOff/arrSig unset: a re-rooted root descriptor then resolved
// through the (empty) descriptor-table branch and bound a ZERO buffer,
// and the root12 CBV draw rendered black with no drop or error. The
// scan must accept naga's suffixed names as well as the bare SPIR-V names.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { scanWgslResources } from '../../../wgsl-utils.mjs';

const ROOT_WGSL = `@group(0) @binding(4096)
var<storage> bw_root_p0_: PhysicalPointerFloat4NonWriteCBVArray;`;
const ARR_WGSL = `@group(5) @binding(0)
var bw_arr_g5_b0_p0_a0_: texture_storage_2d<r16float,read>;`;

test('naga-suffixed root-descriptor name resolves its push offset', () => {
  const [d] = scanWgslResources(ROOT_WGSL);
  assert.equal(d.group, 0);
  assert.equal(d.binding, 4096);
  assert.equal(d.rootOff, 0);
});

test('naga-suffixed descriptor-array clone resolves its push signature', () => {
  const [d] = scanWgslResources(ARR_WGSL);
  assert.equal(d.group, 5);
  assert.equal(d.binding, 0);
  assert.deepEqual(d.arrSig, { kind: 'push', pushOff: 0, add: 0 });
});

test('bare SPIR-V names still resolve (no regression)', () => {
  const [r] = scanWgslResources(ROOT_WGSL.replace('bw_root_p0_', 'bw_root_p0'));
  assert.equal(r.rootOff, 0);
  const [a] = scanWgslResources(ARR_WGSL.replace('bw_arr_g5_b0_p0_a0_', 'bw_arr_g5_b0_p0_a0'));
  assert.deepEqual(a.arrSig, { kind: 'push', pushOff: 0, add: 0 });
});

test('unrelated names do not gain R15 metadata', () => {
  const [d] = scanWgslResources(`@group(0) @binding(0)
var<uniform> plain: Plain;`);
  assert.equal(d.rootOff, undefined);
  assert.equal(d.arrSig, undefined);
});
