// web/tests/suite/unit/vk-psb-serving.test.mjs -- page-tier + bridge pins
// for writable root-descriptor (PSB) serving, lane-psb.
//
// The behavioral proof is the guest run (root12: cbv/srv/uav all OK,
// RESULT 0, evidence test-results/dxvk-psbroot8 on the lane branch).
// These pins guard the three load-bearing tier/bridge decisions that the
// guest run forced, each of which fails SILENTLY if reverted:
//   1. Bind-group-layout visibility follows the stages that actually
//      declare a binding. Dawn rejects a read-write storage buffer whose
//      visibility includes the vertex stage; the root UAV is declared
//      only by the fragment shader and must not inherit VERTEX|FRAGMENT.
//   2. Storage GPUBuffers carry COPY_SRC so a deferred V2_BUFFER_READBACK
//      can copy out of them (Dawn: "(CopyDst|Storage) doesn't include
//      CopySrc" otherwise). Both usage computations must agree.
//   3. The bridge re-stamps a writable root slot's GPU-write mark at
//      submit time inside emitRootDescs: the cleanliness epoch must be
//      measured at the draw's execution point, after the same
//      submission's vkCmdUpdateBuffer initial-data upload has landed.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const tier = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');

test('tier: layout visibility defaults to the declaring stage (stageVis)', () => {
  assert.ok(tier.includes('vis = decl.stageVis ??'), 'layoutEntryFor must default visibility to decl.stageVis');
  assert.ok(tier.includes('stageVis: GPUShaderStage.VERTEX'), 'vertex decls must be tagged VERTEX');
  assert.ok(tier.includes('stageVis: GPUShaderStage.FRAGMENT'), 'fragment decls must be tagged FRAGMENT');
  assert.ok(tier.includes('prior.stageVis |= decl.stageVis'), 'a binding shared by both stages must union visibility');
});

test('tier: storage buffers are created with COPY_SRC at both usage sites', () => {
  const sites = tier.split('GPUBufferUsage.COPY_SRC : 0)').length - 1;
  assert.equal(sites, 2, 'key builder and bind-group entries must both add COPY_SRC for storage');
});

test('bridge: writable root slots are marked at draw record time and re-stamped at submit', () => {
  assert.ok(bridge.includes('vk64MarkDrawRootWrites(st)'), 'pushCmd must mark on draws');
  assert.ok(bridge.includes('vk64MarkGpuWrite(r.obj, r.absOff, r.len, buf->mem, mem->writeEpoch)'),
    'emitRootDescs must re-stamp the mark with the current write epoch');
  assert.ok(bridge.includes('rootWritableSlots'), 'pipeline census must carry writable root slots');
});
