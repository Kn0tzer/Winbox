// web/tests/suite/unit/vk-compute-serving-guard.test.mjs — compute serving
// is wired end to end and the unservable remainder stays loud, campaign
// section 4.27.
//
// Before this lane, CMD_DISPATCH hit a single loud drop in the submit
// emitter and descriptor binds at the COMPUTE point were rejected at
// record time. The serving path is: bridge records binds at both bind
// points, captures the compute stage's specialization + root census at
// pipeline creation, emits PIPELINE_COMPUTE/DISPATCH records, and the
// page tier builds real WebGPU compute pipelines with the graphics
// path's descriptor machinery. What is still NOT served must keep its
// loud names: a dispatch with no (or a non-zero-base) compute pipeline,
// and binds at bind points that are neither graphics nor compute.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');
const protocol = readFileSync(new URL('web/vk-protocol.mjs', root), 'utf8');
const tier = readFileSync(new URL('web/vkwebgpu.mjs', root), 'utf8');

test('the submit emitter serves dispatches and keeps the loud residuals', () => {
  const dispatchCase = /case CMD_DISPATCH: \{[\s\S]*?\n        \}/.exec(bridge);
  assert.ok(dispatchCase, 'CMD_DISPATCH case exists');
  assert.ok(dispatchCase[0].includes('V2_DISPATCH'), 'a served dispatch emits the V2_DISPATCH record');
  assert.ok(dispatchCase[0].includes('vk64Unsupported("compute-dispatch"'),
    'unservable dispatches (no compute pipeline, DispatchBase) stay loud under compute-dispatch');
  assert.ok(dispatchCase[0].includes('emitRootDescs(csPipe)'), 'root descriptors are emitted for compute like graphics');
});

test('descriptor binds are recorded at the compute bind point', () => {
  assert.ok(bridge.includes('args.a[1] != VK_PIPELINE_BIND_POINT_GRAPHICS && args.a[1] != VK_PIPELINE_BIND_POINT_COMPUTE'),
    'both bind gates accept GRAPHICS and COMPUTE');
  assert.ok(bridge.includes('vk64Unsupported("descriptor-bind-point"'),
    'other bind points fail loudly under their own name');
});

test('compute pipeline creation captures spec constants and the root census', () => {
  assert.ok(bridge.includes('captureStageSpec(cis[i].stage.pSpecializationInfo, p->csSpec)'),
    'the compute stage specialization is captured like the graphics stages');
  assert.ok(bridge.includes('V2_PIPELINE_COMPUTE = 0x1E'), 'bridge protocol constant');
});

test('dispatch writes are marked and buffer copies of them defer to the page', () => {
  assert.ok(bridge.includes('vk64MarkComputeWrites(cb)'), 'dispatch handlers mark storage writes');
  assert.ok(bridge.includes('CMD_COPY_B2B_RB'), 'the deferred copy command exists');
  assert.ok(bridge.includes('V2_BUFFER_READBACK = 0x20'), 'bridge protocol constant');
  assert.ok(bridge.includes('vk64FindGpuWrite'), 'copy deferral is gated on a proven GPU write');
});

test('the protocol carries the compute records', () => {
  assert.ok(protocol.includes('PIPELINE_COMPUTE: 0x1E'));
  assert.ok(protocol.includes('DISPATCH: 0x1F'));
  assert.ok(protocol.includes('BUFFER_READBACK: 0x20'));
  assert.ok(protocol.includes("if (stage > 2) throw new Error('vk64: v2 SHADER bad stage')"),
    'SHADER records accept the compute stage and no more');
});

test('the page tier builds and runs real compute pipelines', () => {
  assert.ok(tier.includes('async function getComputePipelineFor('), 'a compute pipeline builder exists');
  assert.ok(tier.includes('device.createComputePipeline('), 'it creates WebGPU compute pipelines');
  assert.ok(tier.includes("case 'dispatch': {"), 'the replay serves dispatch ops');
  assert.ok(tier.includes('cp.dispatchWorkgroups(op.x, op.y, op.z)'), 'dispatches execute with the guest group counts');
  assert.ok(tier.includes('remapDeclGroups(decls, [csR], hasPush, fail)'),
    'compute reuses the graphics descriptor remap, not a parallel one');
  assert.ok(tier.includes('storageBufferRegistry'), 'storage buffers are registered for buffer readbacks');
  assert.ok(tier.includes("noteSkippedDispatch('dispatch-pipeline-skipped'"), 'unservable dispatches are counted, never silent');
});
