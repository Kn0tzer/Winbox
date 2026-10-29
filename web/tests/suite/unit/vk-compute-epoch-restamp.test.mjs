// web/tests/suite/unit/vk-compute-epoch-restamp.test.mjs -- bridge pins
// for the compute GPU-write epoch re-stamp, lane-cepoch.
//
// The GPU-write registry (vk64MarkGpuWrite / vk64FindGpuWrite) decides
// whether a copy out of a dispatch-written storage range defers to a
// page-tier BUFFER_READBACK or falls back to an eager guest memcpy. The
// cleanliness proof compares the mark's epoch against the memory's write
// log. A mark stamped at dispatch RECORD time predates the submission's
// own vkCmdUpdateBuffer initial-data upload (vkd3d puts a resource's
// initial data into the very buffer the dispatch writes, recorded in the
// same command buffer), so that upload -- which precedes the dispatch's
// execution -- poisoned the proof and the readback was refused. The
// graphics sibling of this defect was proven in-guest by lane-psb
// (root12 UAV readback); these pins guard the compute fix's three
// load-bearing decisions, each of which fails SILENTLY if reverted:
//   1. Record-time marking is unchanged: the dispatch handlers still
//      mark through vk64MarkComputeWrites over the full recorded prefix.
//   2. The submit serializer re-stamps the dispatch's marks at its
//      execution point: inside the CMD_DISPATCH case, after the
//      loud-drop checks and before the V2_DISPATCH chunk is emitted,
//      via the SAME prefix-bounded resolution the record path uses.
//   3. The cleanliness proof is not weakened: vk64FindGpuWrite still
//      requires a never-mapped memory and vk64MemRangeCleanSince
//      against the mark's epoch, and vk64MarkGpuWrite still refreshes
//      an existing (buf, off, size) key in place.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');

test('bridge: record-time compute marking still resolves the full recorded prefix', () => {
  assert.ok(bridge.includes('static void vk64MarkComputeWrites(CmdBuf* cb) { vk64MarkComputeWritesUpTo(cb, cb->cmds.size()); }'),
    'vk64MarkComputeWrites must remain the record-time entry over the whole command buffer');
  const calls = bridge.split('vk64MarkComputeWrites(cb);').length - 1;
  assert.equal(calls, 3, 'vkCmdDispatch, vkCmdDispatchBase and vkCmdDispatchIndirect must each mark at record time');
});

test('bridge: the marking replay is prefix-bounded and clamped', () => {
  assert.ok(bridge.includes('static void vk64MarkComputeWritesUpTo(CmdBuf* cb, size_t upto)'),
    'the shared resolution must take a prefix bound');
  assert.ok(bridge.includes('if (upto > cb->cmds.size()) upto = cb->cmds.size();'),
    'the prefix bound must clamp to the recorded stream');
});

test('bridge: submit walk re-stamps the dispatch marks at the execution point', () => {
  const caseStart = bridge.indexOf('case CMD_DISPATCH: {');
  assert.ok(caseStart >= 0, 'serializeSubmitV2 must have a CMD_DISPATCH case');
  const caseEnd = bridge.indexOf('case CMD_COPY_B2B_RB: {', caseStart);
  assert.ok(caseEnd > caseStart, 'the dispatch case must precede the deferred-copy case');
  const block = bridge.slice(caseStart, caseEnd);
  const restamp = block.indexOf('vk64RestampComputeWrites(st, (size_t)(&k - st->cmds.data()));');
  assert.ok(restamp >= 0, 'the dispatch case must re-stamp with the dispatch command index in the serialized buffer');
  const loudDrop = block.indexOf('vkCmdDispatchBase with a non-zero base group');
  assert.ok(loudDrop >= 0 && loudDrop < restamp,
    'the re-stamp must come after the loud-drop checks: a dropped dispatch never re-stamps');
  const emitDispatch = block.indexOf('emit(V2_DISPATCH');
  assert.ok(emitDispatch > restamp, 'the re-stamp must land before the V2_DISPATCH chunk is emitted');
});

test('bridge: the re-stamp reuses the record-time resolution, keyed refresh in place', () => {
  assert.ok(bridge.includes('static void vk64RestampComputeWrites(CmdBuf* cb, size_t dispatchIdx)'),
    'the submit-time entry must exist');
  assert.ok(bridge.includes('vk64MarkComputeWritesUpTo(cb, dispatchIdx);'),
    'the re-stamp must resolve the dispatch prefix through the shared replay');
  assert.ok(bridge.includes('if (m.buf == buf && m.off == off && m.size == size) { m.mem = mem; m.epoch = epoch; return; }'),
    'vk64MarkGpuWrite must refresh an existing (buf, off, size) mark in place');
});

test('bridge: the cleanliness proof is not weakened', () => {
  assert.ok(bridge.includes('if (!mem || mem->wasMapped) continue;'),
    'vk64FindGpuWrite must still refuse ranges in guest-mapped memory');
  assert.ok(bridge.includes('if (!vk64MemRangeCleanSince(mem, m.epoch, absOff, size)) continue;'),
    'vk64FindGpuWrite must still require no logged write since the mark epoch');
});
