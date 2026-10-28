// web/tests/suite/unit/vk-storage-texel.test.mjs — V2 storage texel buffer
// serving (metabind lane, campaign section 4.31).
//
// vkd3d-proton's internal meta pipelines (typed buffer clears/copies) bind
// their storage texel buffer through vkCmdPushDescriptorSetKHR. The bridge
// recorded the write but wired the binding with kind 0 (no V2 kind existed),
// emitted no V2_BUFFER_VIEW record and no view bytes, and never marked the
// view's range GPU-written — so the meta dispatch reached the page tier
// unservable and the guest read zeros back. Storage texel bindings now
// serialize as wire kind 6 with the view record and its bytes, dispatches
// through them mark the view range like a storage buffer's, and the page
// tier serves the view as a wrapped 2D storage texture whose writes read
// back byte-exactly through V2_BUFFER_READBACK. These tests pin the wire
// decode and the source-level shape of every hop; the spirvfix half lives
// in spirvfix-texel-buffer.test.mjs (R12S), the record-level behaviour in
// the native harness (vk64_native_test.cpp).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';

const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
const page = readFileSync(new URL('../../../../web/vkwebgpu.mjs', import.meta.url), 'utf8');

function u8s(...v) { return new Uint8Array(v); }
function u16s(...v) { const b = new Uint8Array(v.length * 2); const d = new DataView(b.buffer); v.forEach((x, i) => d.setUint16(i * 2, x, true)); return b; }
function u32s(...v) { const b = new Uint8Array(v.length * 4); const d = new DataView(b.buffer); v.forEach((x, i) => d.setUint32(i * 4, x, true)); return b; }
function u64s(...v) { const b = new Uint8Array(v.length * 8); const d = new DataView(b.buffer); v.forEach((x, i) => d.setBigUint64(i * 8, BigInt(x), true)); return b; }
function cat(...parts) {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let at = 0; for (const p of parts) { out.set(p, at); at += p.length; }
  return out;
}

test('wire: a kind-6 DESC_SET binding, its BUFFER_VIEW and a BUFFER_READBACK decode intact', () => {
  const viewId = 0x2a, bufId = 0x17;
  const descSet = cat(
    u8s(0, 0), u16s(1), u64s(0x99),
    u32s(0, 5, 0, 1), u8s(6, 0), u16s(0), u64s(viewId, 256, 8192));
  const bufView = cat(u64s(viewId, bufId), u32s(101, 0), u64s(8192, 256));
  const readback = cat(u64s(bufId, 8192, 256), u32s(4096, 8192));
  const f = decodeV2Frame([
    { type: V2_REC.FRAME_BEGIN, payload: u32s(7, 64, 64, 0) },
    { type: V2_REC.BUFFER_VIEW, payload: bufView },
    { type: V2_REC.BIND_SETS, payload: cat(u8s(0, 1), u16s(0), u64s(0x55)) },
    { type: V2_REC.DESC_SET, payload: descSet },
    { type: V2_REC.DISPATCH, payload: u32s(4, 1, 1) },
    { type: V2_REC.BUFFER_READBACK, payload: readback },
    { type: V2_REC.FRAME_END, payload: u32s(7) },
  ]);
  const view = f.bufferViews.get('000000000000002a');
  assert.ok(view, 'the buffer view record must decode');
  assert.equal(view.bufferId, '0000000000000017');
  assert.equal(view.format, 101, 'VK_FORMAT_R32G32_UINT rides the view record');
  assert.equal(view.absOff, '8192');
  assert.equal(view.len, '256');
  const ops = f.passes.flatMap(p => p.ops);
  const ds = ops.find(o => o.op === 'descSet');
  assert.ok(ds, 'the desc set op must decode');
  assert.equal(ds.bindings.length, 1);
  assert.equal(ds.bindings[0].kind, 6, 'the storage texel binding keeps wire kind 6 (was: 0, unresolvable)');
  assert.equal(ds.bindings[0].obj, '000000000000002a');
  assert.ok(ops.some(o => o.op === 'dispatch'), 'the dispatch op must decode');
  assert.equal(f.bufReadbacks.length, 1);
  assert.equal(f.bufReadbacks[0].srcBuffer, '0000000000000017');
  assert.equal(Number(f.bufReadbacks[0].size), 256);
});

test('bridge: storage texel writes record the view and no longer log a class rejection', () => {
  const start = bridge.indexOf('static void applyDescWrite');
  const write = bridge.slice(start, bridge.indexOf('\n}\n', start));
  const stoAt = write.indexOf('case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:');
  const sto = write.slice(stoAt, write.indexOf('break;', stoAt));
  assert.match(sto, /b\.obj = rdq\(w\.pTexel \+ at\)/, 'the write must record the buffer view id');
  assert.ok(!sto.includes('vk64Unsupported'), 'the write path must not reject storage texel descriptors as a class');
  assert.ok(!bridge.includes('texel-buffer-storage'),
    'the texel-buffer-storage rejection is retired everywhere (its premise — writes unreachable — is gone)');
});

test('bridge: emitDescSet wires kind 6 with the view record, its bytes, and view-range addressing', () => {
  const start = bridge.indexOf('auto emitDescSet =');
  const emit = bridge.slice(start, bridge.indexOf('U64 activePipe = 0;', start));
  assert.match(emit, /case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: kind = 6; break;/);
  assert.match(emit, /else if \(kind == 5 \|\| kind == 6\)/, 'kind 6 takes its absOff/len from the view, like kind 5');
  assert.match(emit, /b\.type != VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER &&\s*b\.type != VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER/, 'the BUFFER_VIEW pre-loop covers storage texel views');
  const dataArm = emit.slice(emit.indexOf('VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||'));
  assert.match(dataArm, /emitBufferData\(bv->buffer, vbuf->memOff \+ bv->offset, bv->range\)/, 'the view range bytes are emitted as seed data');
});

test('bridge: a dispatch through a storage texel binding marks the view range GPU-written', () => {
  const start = bridge.indexOf('static void vk64MarkComputeWrites');
  const mark = bridge.slice(start, bridge.indexOf('\n}\n', start));
  assert.match(mark, /b\.type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER/);
  assert.match(mark, /vk64MarkGpuWrite\(bv->buffer, buf->memOff \+ bv->offset, bv->range, buf->mem, mem->writeEpoch\)/,
    'the view range is epoch-marked so a later copy defers to a page readback');
});

test('page: kind 6 resolves in the format signature, the bind group, its fingerprint and the readback', () => {
  const sig = page.slice(page.indexOf('function storageSignatureFor'), page.indexOf('function samplerIdForDecl'));
  assert.match(sig, /b\.kind === 6/, 'the R14 signature resolves storage texel views');
  assert.match(sig, /V2_STORAGE_FORMAT\[bv\.format\]/, 'the signature takes the spv format from the bound view');
  const entriesAt = page.indexOf('function bindGroupEntries');
  const entries = page.slice(entriesAt, page.indexOf('async function render', entriesAt));
  assert.match(entries, /b\.kind === 6/, 'bind-group construction serves kind 6');
  assert.match(entries, /storageTexelRegistry\.set\(/, 'served views register for readback resolution');
  assert.match(entries, /noteDrop\('bind-group-storage-texel-unavailable'/, 'an unmaterialisable view still drops loudly');
  const fp = page.slice(page.indexOf('function bindGroupFingerprint'), page.indexOf('function bindGroupEntries'));
  assert.match(fp, /b\.kind === 6/, 'the fingerprint keys kind 6 on its content');
  assert.match(fp, /storageTexelTextureKey\(b\.obj\)/, 'the fingerprint uses the storage texel content key');
  assert.match(page, /function storageTexelTextureFor\(viewHex\)/, 'the storage texel materialiser exists');
  assert.match(page, /GPUTextureUsage\.STORAGE_BINDING \| GPUTextureUsage\.COPY_DST \| GPUTextureUsage\.COPY_SRC/,
    'the materialised texture is writable by the dispatch and readable by the readback copy');
  const rb = page.slice(page.indexOf('const v2bufreadbacks = []'), page.indexOf('// Readback of the present target'));
  assert.match(rb, /storageTexelRegistry\.values\(\)/, 'the readback encoder falls back to storage texel textures');
  assert.match(rb, /copyTextureToBuffer/, 'the readback copies out of the storage texture');
  assert.match(page, /src\.subarray\(vbr\.srcSkip \|\| 0/, 'the resolver honours the in-texture skip');
});
