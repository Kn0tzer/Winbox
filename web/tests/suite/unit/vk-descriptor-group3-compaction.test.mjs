// web/tests/suite/unit/vk-descriptor-group3-compaction.test.mjs —
// guest descriptor set V2_PUSH_GROUP compaction, campaign section 4.32.
//
// Group 3 is the page tier's reserved push-constant group: the draw
// loop binds the push UBO there separately and never binds a
// descriptor set there, so before this lane any non-push declaration
// at group 3 failed the pipeline ('group-3-non-push'). But guest set
// numbers are virtual, and vkd3d heap-lowering legitimately places a
// descriptor table at set 3 -- cs12 TEX pass 2's sampled-image array
// lands there (set 3 binding 0, next to the storage array at set 5),
// so the tier rejected a real, servable module one stage after the
// format-injection fix (section 4.31) let it translate. The fix:
// planGroupRemap (wgsl-utils.mjs) compacts a guest set numbered 3
// like any other set -- the proven push declaration (group 3,
// binding 0, uniform, with a real push block) keeps group 3, the
// guest declarations move to a free slot and their WGSL declarations
// are moved by name (rewriteGuestGroupDeclWGSL), because a
// group-number rewrite cannot split one @group between two targets.
// Fail-closed residues: a storage buffer in the push slot stays
// 'group-3-non-push' (the slot's only legitimate buffer occupant is
// the push UBO), and over-budget pipelines stay 'group-count-limit'.
// Pipelines with no guest declarations at group 3 get exactly the
// pre-4.32 assignment.
//
// The in-guest cs12 TEX proof (bad=0) is a MERGE-TIME gate: this
// branch's base predates the compute merge, so the lane-level proof
// is the real captured pass-2 module through the offline pipeline
// (spirvfix -> naga -> push rewrite -> scan -> plan -> WGSL rewrite),
// plus synthetic variants and the tier wiring pins.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applySpirvFix } from '../../../../tools/spirvfix/spirvfix.mjs';
import {
  planGroupRemap, rewriteGuestGroupDeclWGSL, rewritePushConstantsWGSL, scanWgslResources,
} from '../../../wgsl-utils.mjs';
import { V2_PUSH_GROUP } from '../../../vk-protocol.mjs';
import * as naga from '../../../vendor/naga/node.js';

const tier = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');

const pass1 = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-cs-uav-heap-write-unknown-pass1.spv', import.meta.url)));
const pass2 = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/d3d12-cs-uav-heap-write-unknown-pass2.spv', import.meta.url)));

// The offline pipeline the page tier runs, up to the remap: spirvfix
// with the storage-format signature the format resolver produces for
// the captured descriptor state (section 4.31), naga translation, the
// push-constant rewrite, and the resource scan.
const translateAndScan = (bytes) => {
  const wgsl = String(naga.translate({
    from: 'spirv', to: 'wgsl', source: applySpirvFix(bytes, { storageFormats: '5:0=4' }).bytes,
  }));
  const rw = rewritePushConstantsWGSL(wgsl);
  return { code: rw.code, hasPush: rw.pushBlocks > 0, decls: scanWgslResources(rw.code) };
};

// Mirror of the tier's application order: group-number rewrite first
// (in the split case it keeps group 3 at 3), then the guest
// declarations move out by name.
const applyPlanToWgsl = (code, plan) => {
  let out = code.replace(/@group\((\d+)\)/g, (m, n) => '@group(' + (plan.numMap[+n] ?? +n) + ')');
  for (const d of plan.splitDecls) out = rewriteGuestGroupDeclWGSL(out, d, V2_PUSH_GROUP, plan.guest3Target);
  return out;
};

const decl = (group, binding, addr, type, name) => ({
  group, binding, addr, type, name: name || ('v' + group + '_' + binding), arrayCount: 1,
});
const pushDecl = () => decl(3, 0, 'uniform', 'RootConstants', 'registers');

test('real fixture: cs12 TEX pass 2 compacts its set-3 sampled array, push stays', () => {
  const { code, hasPush, decls } = translateAndScan(pass2);
  assert.equal(hasPush, true);
  assert.equal(decls.length, 3);
  const push = decls.find(d => d.name === 'registers');
  const arr3 = decls.find(d => d.name.startsWith('bw_arr_g3_'));
  const arr5 = decls.find(d => d.name.startsWith('bw_arr_g5_'));
  assert.ok(push && arr3 && arr5, 'the captured shape: push + set-3 sampled clone + set-5 storage clone');
  assert.equal(arr3.group, 3); assert.equal(arr3.binding, 0);
  assert.ok(arr3.type.startsWith('texture_2d'), 'the set-3 declaration is a sampled texture, never a push shape');

  const plan = planGroupRemap(decls, 4, hasPush);
  assert.equal(plan.error, null);
  assert.equal(plan.targets[decls.indexOf(push)], 3, 'the push declaration keeps group 3');
  assert.equal(plan.targets[decls.indexOf(arr3)], 0, 'guest set 3 compacts to the first free slot');
  assert.equal(plan.targets[decls.indexOf(arr5)], 1, 'set 5 compacts after it');
  assert.deepEqual(plan.groupMap, { 0: 3, 1: 5, 3: 3 }, 'draw-time lookups still resolve the original sets');
  assert.deepEqual(plan.splitDecls, [arr3]);
  assert.equal(plan.guest3Target, 0);

  const rewritten = applyPlanToWgsl(code, plan);
  const after = scanWgslResources(rewritten);
  const byName = Object.fromEntries(after.map(d => [d.name, d]));
  assert.equal(byName['registers'].group, 3, 'push declaration untouched by the split rewrite');
  assert.equal(byName['registers'].addr, 'uniform');
  assert.equal(byName[arr3.name].group, 0, 'the set-3 clone moved to its compacted group');
  assert.equal(byName[arr3.name].binding, 0, 'bindings within the set are unchanged');
  assert.equal(byName[arr5.name].group, 1);
  assert.ok(!after.some(d => d.group === 3 && d.name !== 'registers'), 'only the push declaration remains at group 3');
});

test('real fixture: pass 1 (no guest set 3) keeps the pre-4.32 assignment', () => {
  const { hasPush, decls } = translateAndScan(pass1);
  const plan = planGroupRemap(decls, 4, hasPush);
  assert.equal(plan.error, null);
  assert.deepEqual(plan.splitDecls, []);
  assert.equal(plan.guest3Target, null);
  const push = decls.find(d => d.name === 'registers');
  const arr5 = decls.find(d => d.name.startsWith('bw_arr_g5_'));
  assert.equal(plan.targets[decls.indexOf(push)], 3);
  assert.equal(plan.targets[decls.indexOf(arr5)], 0);
  assert.deepEqual(plan.groupMap, { 0: 5, 3: 3 });
});

test('synthetic: guest set 3 alongside a real push declaration', () => {
  const decls = [
    pushDecl(),
    decl(3, 2, '', 'texture_2d<f32>'),
    decl(3, 3, '', 'sampler'),
    decl(0, 0, 'uniform', 'Globals'),
  ];
  const plan = planGroupRemap(decls, 4, true);
  assert.equal(plan.error, null);
  assert.deepEqual(plan.targets, [3, 1, 1, 0], 'push stays; guest set 3 compacts as one set; set 0 unchanged');
  assert.deepEqual(plan.groupMap, { 0: 0, 1: 3, 3: 3 });
  assert.deepEqual(plan.splitDecls, [decls[1], decls[2]]);
});

test('synthetic: set-3-only module with no push block needs no split', () => {
  const decls = [decl(3, 0, '', 'texture_2d<f32>'), decl(3, 1, '', 'sampler')];
  const plan = planGroupRemap(decls, 4, false);
  assert.equal(plan.error, null);
  assert.deepEqual(plan.targets, [0, 0]);
  assert.deepEqual(plan.groupMap, { 0: 3 });
  assert.equal(plan.numMap[3], 0, 'the number rewrite alone moves a purely-guest group 3');
  assert.deepEqual(plan.splitDecls, []);
});

test('synthetic: a uniform buffer at the push slot is guest only when no push block exists', () => {
  const guestUbo = () => decl(3, 0, 'uniform', 'GuestGlobals', 'guest_globals');
  const withPush = planGroupRemap([guestUbo()], 4, true);
  assert.equal(withPush.error, null);
  assert.deepEqual(withPush.targets, [3], 'with a real push block, the proven shape is the push declaration');
  const noPush = planGroupRemap([guestUbo()], 4, false);
  assert.equal(noPush.error, null);
  assert.deepEqual(noPush.targets, [0], 'with no push block it is provably a guest buffer and compacts');
  assert.deepEqual(noPush.groupMap, { 0: 3 });
});

test('fail-closed: a storage buffer in the push slot stays group-3-non-push', () => {
  const storageAtSlot = () => decl(3, 0, 'storage,read_write', 'ByteAddressBuffer', 'slot_buffer');
  assert.equal(planGroupRemap([storageAtSlot()], 4, false).error, 'group-3-non-push');
  assert.equal(planGroupRemap([storageAtSlot()], 4, true).error, 'group-3-non-push');
  assert.equal(planGroupRemap([pushDecl(), storageAtSlot()], 4, true).error, 'group-3-non-push',
    'even alongside a proven push declaration, a second buffer in its slot is ambiguous');
  // A storage buffer at any OTHER binding of set 3 is a plain guest resource.
  const ok = planGroupRemap([pushDecl(), decl(3, 4, 'storage,read_write', 'ByteAddressBuffer')], 4, true);
  assert.equal(ok.error, null);
  assert.deepEqual(ok.targets, [3, 0]);
});

test('fail-closed: budget exhaustion stays group-count-limit', () => {
  const over = planGroupRemap([
    pushDecl(), decl(0, 0, 'uniform', 'A'), decl(1, 0, 'uniform', 'B'),
    decl(2, 0, 'uniform', 'C'), decl(3, 1, '', 'texture_2d<f32>'),
  ], 4, true);
  assert.equal(over.error, 'group-count-limit');
  assert.equal(over.count, 5, 'four guest sets plus the push group exceed the 4-group budget');
  const exact = planGroupRemap([
    pushDecl(), decl(0, 0, 'uniform', 'A'), decl(1, 0, 'uniform', 'B'), decl(3, 1, '', 'texture_2d<f32>'),
  ], 4, true);
  assert.equal(exact.error, null);
  assert.deepEqual(exact.targets, [3, 0, 1, 2]);
  assert.equal(exact.count, 4);
});

test('non-regression: pipelines without guest group 3 keep the legacy assignment', () => {
  const a = planGroupRemap([decl(0, 0, 'uniform', 'A'), decl(2, 0, 'uniform', 'B'), decl(5, 0, '', 'texture_2d<f32>')], 4, false);
  assert.deepEqual(a.targets, [0, 1, 2]);
  assert.deepEqual(a.groupMap, { 0: 0, 1: 2, 2: 5 });
  const b = planGroupRemap([decl(0, 0, 'uniform', 'A'), pushDecl(), decl(5, 0, '', 'texture_2d<f32>')], 4, true);
  assert.deepEqual(b.targets, [0, 3, 1], 'set 5 still lands on slot 1, exactly as before');
  assert.deepEqual(b.groupMap, { 0: 0, 1: 5, 3: 3 });
});

test('tier wiring: the pipeline builder plans through the shared helper', () => {
  assert.ok(tier.includes('planGroupRemap(decls, device.limits?.maxBindGroups || 4, hasPush)'),
    'the builder delegates group planning to planGroupRemap');
  assert.ok(tier.includes('rewriteGuestGroupDeclWGSL(vsR.code, d, V2_PUSH_GROUP, plan.guest3Target)'),
    'vertex WGSL split rewrite is wired');
  assert.ok(tier.includes('rewriteGuestGroupDeclWGSL(fsR.code, d, V2_PUSH_GROUP, plan.guest3Target)'),
    'fragment WGSL split rewrite is wired');
  assert.ok(!tier.includes('if (d.group === V2_PUSH_GROUP && !(d.binding === V2_PUSH_BINDING && d.addr === \'uniform\'))'),
    'the blanket group-3 rejection is gone');
});
