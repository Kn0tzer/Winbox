// web/tests/suite/unit/vk-compute-root-marks.test.mjs -- bridge pins for
// compute root-descriptor write marking (lane-crootmark).
//
// The behavioral proof is the native harness block "compute
// root-descriptor dispatch writes are marked, re-stamped at submit, and
// later dirt still refuses deferral" in tools/vkd3d/tests/
// vk64_native_test.cpp (a root-UAV compute module dispatched against a
// registered device address). These pins guard the source structure
// that behavior rests on, each of which fails SILENTLY if reverted:
//   1. The compute marking core calls the root census, after the
//      descriptor-set marking -- a census living anywhere else falls
//      out of the submit-time re-stamp discipline. The call passes the
//      core's whole-list bound on this base; the census takes a prefix
//      bound so a bounded submit-time re-run re-marks identically.
//   2. The census mirrors the draw census exactly: same VA resolution
//      through the device-address registry, same fail-closed skips,
//      same (buf, off, size) mark key -- a different key would never be
//      refreshed in place by emitRootDescs' re-stamp.
//   3. The gate is a compute pipeline (pipe->cs): a dispatch never
//      executes a graphics pipeline, whose root slots belong to the
//      draw census.
//   4. The cleanliness proof the marks feed is unweakened: the
//      never-mapped requirement and the epoch cleanliness check stand,
//      and both copy handlers still gate deferral on a proven mark.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
const nativeTest = readFileSync(new URL('../../../../tools/vkd3d/tests/vk64_native_test.cpp', import.meta.url), 'utf8');

const bodyOf = (src, startMarker, endMarker) => {
  const a = src.indexOf(startMarker);
  assert.ok(a >= 0, 'missing start marker: ' + startMarker);
  const b = src.indexOf(endMarker, a);
  assert.ok(b > a, 'missing end marker after: ' + startMarker);
  return src.slice(a, b);
};

test('bridge: the compute marking core calls the root census after the set marking', () => {
  const core = bodyOf(bridge, 'static void vk64MarkComputeWrites(CmdBuf* cb) {',
    '// Draw-time sibling of vk64MarkComputeWrites');
  assert.ok(core.includes('vk64MarkComputeRootWrites(cb, cb->cmds.size());'),
    'vk64MarkComputeWrites must call the root census');
  const setMark = core.indexOf('vk64MarkGpuWrite(b.obj, buf->memOff + off, len, buf->mem, mem->writeEpoch)');
  const censusCall = core.indexOf('vk64MarkComputeRootWrites(cb, cb->cmds.size());');
  assert.ok(setMark >= 0 && censusCall > setMark,
    'the root census runs after the descriptor-set marking, inside the same core');
});

test('bridge: the compute root census mirrors the draw census (resolution, key, fail-closed skips)', () => {
  const census = bodyOf(bridge, 'static void vk64MarkComputeRootWrites(CmdBuf* cb, size_t upto) {',
    '// A deferred buffer->buffer copy planned at submit');
  assert.ok(census.includes('!pipe->cs'), 'the census requires a compute pipeline');
  assert.ok(census.includes('if (upto > cb->cmds.size()) upto = cb->cmds.size();'),
    'the census clamps the prefix bound');
  assert.ok(census.includes('for (size_t ci = 0; ci < upto; ci++)'),
    'the census replay honours the prefix bound');
  assert.ok(census.includes('pipe->rootWritableSlots'), 'the census walks the pipeline writable root slots');
  assert.ok(census.includes('g_devVaRegistry.upper_bound(va)'), 'VAs resolve through the device-address registry');
  assert.ok(census.includes('if (!va) continue;'), 'a zero VA leaves the slot unmarked');
  assert.ok(census.includes('va - it->first >= buf->size'), 'an out-of-range VA leaves the slot unmarked');
  const keyLine = 'vk64MarkGpuWrite(it->second, buf->memOff + boff, buf->size - boff, buf->mem, mem->writeEpoch);';
  assert.ok(census.includes(keyLine), 'the census marks with the draw census key');
  assert.equal(bridge.split(keyLine).length - 1, 2,
    'exactly two censuses mark root ranges with this key: draw and compute');
});

test('bridge: submit-time root resolution re-stamps the marks a dispatch leaves', () => {
  assert.ok(bridge.includes('emitRootDescs(csPipe);'),
    'the CMD_DISPATCH serializer case resolves root descriptors like draws');
  assert.ok(bridge.includes('vk64MarkGpuWrite(r.obj, r.absOff, r.len, buf->mem, mem->writeEpoch)'),
    'emitRootDescs re-stamps writable root slots with the current write epoch');
});

test('bridge: the cleanliness proof the marks feed is unweakened', () => {
  const proof = bodyOf(bridge, 'static const GpuWriteMark* vk64FindGpuWrite', 'static void vk64MarkComputeRootWrites(CmdBuf* cb, size_t upto);');
  assert.ok(proof.includes('mem->wasMapped'), 'mapped memory never defers');
  assert.ok(proof.includes('vk64MemRangeCleanSince(mem, m.epoch, absOff, size)'),
    'deferral requires the range provably untouched since the mark epoch');
  assert.ok(bridge.includes('vk64FindGpuWrite(args.a[1], src->memOff + r.srcOffset, n)'),
    'vkCmdCopyBuffer defers only on a proven mark');
  assert.ok(bridge.includes('vk64FindGpuWrite(hSrc2, src->memOff + r.srcOffset, n)'),
    'vkCmdCopyBuffer2 defers only on a proven mark');
});

test('native: the behavioral guard block is present in the vk64 harness', () => {
  assert.ok(nativeTest.includes('PASS: compute root-descriptor dispatch writes are marked'),
    'the native harness carries the root-mark behavior block');
  assert.ok(nativeTest.includes('csRootPipe->rootWritableSlots'),
    'the native block censuses a writable root slot onto the compute pipeline');
  assert.ok(nativeTest.includes('vk64FindGpuWrite(offBufId, 8192 + 64, 128)'),
    'the native block pins the root mark at the memory-absolute offset');
  assert.ok(nativeTest.includes('vk64FindGpuWrite(setBufId, 8192 + 128, 512)'),
    'the native block pins the descriptor-set mark at the memory-absolute offset');
  assert.ok(nativeTest.includes('vk64FindGpuWrite(offBufId, 64, 128) == nullptr'),
    'the native block asserts the buffer-relative offset stays unmarked');
});
