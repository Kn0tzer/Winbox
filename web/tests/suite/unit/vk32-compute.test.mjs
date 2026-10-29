// web/tests/suite/unit/vk32-compute.test.mjs — 32-bit compute serving,
// campaign section 4.40.
//
// Through the D3D12 campaign the 64-bit ABI served compute end to end
// (section 4.30) while the 32-bit ABI recorded nothing a dispatch needs:
// vkCreateComputePipelines minted bare pipeline objects (no stage module
// recorded), both descriptor bind gates dropped COMPUTE binds loudly
// ("compute-descriptor-sets"), dispatches were never write-marked, the
// DispatchBase base groups were discarded, and buffer copies never
// deferred to a page readback. Every 32-bit dispatch therefore died at
// submit as "compute-dispatch". The 32-bit handlers now mirror the
// 64-bit ones and feed the same shared records (PIPELINE_COMPUTE /
// DISPATCH / BUFFER_READBACK carry no ABI provenance — the page tier
// executes them identically; decoding is pinned by
// vk-compute-records.test.mjs and execution by
// vk-compute-serving-guard.test.mjs). The behavioral proof is the
// native guard "32-bit compute dispatch is recorded, marked, deferred
// and served" in tools/vkd3d/tests/vk64_native_test.cpp; these pins
// keep the mirror from silently unravelling.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');

// The 32-bit trap switch: everything from vk32Bridge to end of file.
const bridge32 = bridge.slice(bridge.indexOf('U64 vk32Bridge('));
assert.ok(bridge32.length > 0, 'vk32Bridge exists');

function caseBody(region, marker, span = 4000) {
  const at = region.indexOf(marker);
  assert.ok(at >= 0, `case exists: ${marker}`);
  return region.slice(at, at + span);
}

test('32-bit compute pipeline creation records the stage like the 64-bit handler', () => {
  const body = caseBody(bridge32, 'case VK64_fn_vkCreateComputePipelines:');
  assert.ok(body.includes('p->cs = cis[i].stage.module'), 'the compute stage module is recorded');
  assert.ok(body.includes('p->layout = cis[i].layout'), 'the pipeline layout is recorded');
  assert.ok(body.includes('captureStageSpec32(cis[i].stage.pSpecializationInfo, p->csSpec)'),
    'specialization constants are captured with the 32-bit reader');
  assert.ok(body.includes('r15::analyzeRoot(rm)'), 'the R15 root census runs at creation');
  assert.ok(body.includes('wr64_32((U32)args.a[5] + i * 8, id)'),
    'pipeline ids are written to the shim out slot a[5] (a[4] is the allocator)');
});

test('32-bit descriptor binds are recorded at the compute bind point', () => {
  const dual = 'args.a[1] != VK_PIPELINE_BIND_POINT_GRAPHICS && args.a[1] != VK_PIPELINE_BIND_POINT_COMPUTE';
  assert.equal(bridge32.split(dual).length - 1, 2,
    'both 32-bit bind gates (BindDescriptorSets, PushDescriptorSetKHR) accept GRAPHICS and COMPUTE');
  assert.ok(!bridge.includes('compute-descriptor-sets'),
    'the old no-compute-path decline name is gone from both ABIs');
});

test('32-bit dispatches mark writes and keep the DispatchBase base groups', () => {
  const body = caseBody(bridge32, 'case VK64_fn_vkCmdDispatch:', 3000);
  assert.ok(body.includes('vk64MarkComputeWrites(cb)'),
    'dispatch handlers mark storage writes before the dispatch joins the stream');
  assert.ok(body.includes('c.d = args.a[1]'), 'the base group X is recorded');
  assert.ok(body.includes('c.dispatchBaseY = (U32)args.a[2]') && body.includes('c.dispatchBaseZ = (U32)args.a[3]'),
    'the base groups Y/Z are recorded so a non-zero base declines loudly at submit');
});

test('32-bit buffer copies defer dispatch-written ranges to a page readback', () => {
  const v1 = caseBody(bridge32, 'case VK64_fn_vkCmdCopyBuffer: {', 3500);
  assert.ok(v1.includes('vk64FindGpuWrite(args.a[1], sRel, n)'), 'vkCmdCopyBuffer checks the GPU-write registry');
  assert.ok(v1.includes('CMD_COPY_B2B_RB'), 'vkCmdCopyBuffer records the deferred copy');
  const v2 = caseBody(bridge32, 'case VK64_fn_vkCmdCopyBuffer2:', 3500);
  assert.ok(v2.includes('vk64FindGpuWrite(src, sRel, n)'), 'vkCmdCopyBuffer2/KHR checks the GPU-write registry');
  assert.ok(v2.includes('CMD_COPY_B2B_RB'), 'vkCmdCopyBuffer2/KHR records the deferred copy');
});

test('the submit emitter no longer treats 32-bit dispatches as stageless', () => {
  assert.ok(!bridge.includes('every 32-bit dispatch'),
    'the stale serializer comment claiming the 32-bit ABI never records a compute stage is gone');
  const dispatchCase = /case CMD_DISPATCH: \{[\s\S]*?\n        \}/.exec(bridge);
  assert.ok(dispatchCase && dispatchCase[0].includes('emit(V2_DISPATCH'),
    'the shared CMD_DISPATCH case still emits the dispatch record');
});
