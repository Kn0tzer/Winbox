import test from 'node:test';
import assert from 'node:assert/strict';
import { createVkFrameCapture, captureValue } from '../../../vk-frame-capture.mjs';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

const frame = frameNo => ({ frameNo, width: 4, height: 4, shaders: new Map(), images: new Map(), views: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
  passes: [{ w: 4, h: 4, clearColor: [1, 0, 0, 1], clearDepth: 1, ops: [], attachments: [{ imageId: '0000000000000012', format: 37, loadOp: 1, storeOp: 0 }] }] });

test('capture selects a whole presentation by guest submission ID and freezes raw and decoded state', () => {
  const c = createVkFrameCapture(); c.arm(42);
  const skipped = frame(40); c.wire(Uint8Array.of(1), 0); assert.equal(c.batch(skipped), false);
  const a = frame(41), b = frame(42), bytes = Uint8Array.of(3, 4);
  a.buffers.set('buffer', bytes);
  c.wire(bytes, 1); c.wire(Uint8Array.of(5), 0);
  const batch = { ...b, submissions: [a, b] };
  assert.equal(c.batch(batch, { swapchains: [{ image: '18' }] }), true);
  bytes[0] = 99; a.buffers.clear();
  assert.deepEqual(c.report.submissionIds, [41, 42]);
  assert.equal(c.report.wire[0].bytes.$bytes, 'AwQ=');
  assert.equal(c.report.decoded.submissions[0].buffers.$map[0][1].$bytes, 'AwQ=');
  c.finish(frame(50)); assert.equal(c.report.status, 'rendering', 'an unrelated render finishing cannot complete the selected capture');
  c.finish(batch); assert.equal(c.report.status, 'complete');
});

test('missed selection is an explicit error instead of waiting forever for an already dropped frame', () => {
  const c = createVkFrameCapture(); c.arm(4); c.wire(Uint8Array.of(1), 0);
  c.batch(frame(5)); assert.equal(c.report.status, 'error'); assert.match(c.report.error, /not transported/);
});

test('bounded capture discloses every omitted byte and ignores all traffic until armed', () => {
  const c = createVkFrameCapture({ maxBytes: 4 });
  c.wire(new Uint8Array(100), 0); assert.equal(c.batch(frame(0)), false); assert.equal(c.report, null);
  c.arm(); c.wire(new Uint8Array(5), 0); c.batch(frame(0));
  c.image(frame(99), { stage: 'unrelated' }, new Uint8Array(100));
  assert.equal(c.report.omittedWireBytes, 5); assert.equal(c.report.images.length, 0);
  assert.throws(() => c.arm(1), /rendering/);
  assert.deepEqual(captureValue(2n), { $u64: '2' });
});

test('selected replay reads every pass target, present source, and the actual canvas separately', async () => {
  const env = createValidationDevice();
  const c = createVkFrameCapture(); const f = frame(8); c.arm(8); c.batch(f);
  try {
    const r = await createVkRenderer2(env.canvas, { capture: c });
    assert.equal((await r.render(f)).rendered, true);
    assert.deepEqual(c.report.images.map(i => i.stage).sort(), ['canvas', 'pass', 'present-source']);
    for (const i of c.report.images) { assert.equal(i.submission, 8); assert.equal(i.w, 4); assert.equal(i.h, 4); assert.equal(i.data.byteLength, 1024); }
    assert.equal(c.report.images.find(i => i.stage === 'pass').imageId, '0000000000000012');
    c.finish(f); assert.equal(c.report.status, 'complete');
    r.destroy();
  } finally { env.restore(); }
});

test('capture detects a missing raw prefix and corruption instead of claiming a complete frame', () => {
  const c = createVkFrameCapture({ isBoundary: () => false }); const f = frame(3);
  c.arm(3); c.batch(f); c.finish(f);
  assert.equal(c.report.status, 'error'); assert.match(c.report.error, /prefix/);
  const d = createVkFrameCapture(); d.arm(3); d.wire(Uint8Array.of(1), 1); d.fail('wire gap');
  assert.equal(d.report.status, 'error'); assert.equal(d.report.wire[0].bytes.byteLength, 1); assert.match(d.report.error, /wire gap/);
});

test('readback allocation uses the shared byte budget before reserving memory and retries clear prior images', () => {
  const c = createVkFrameCapture({ maxBytes: 4096 }); const f = frame(9); c.arm(9); c.batch(f);
  assert.equal(c.reserve(f, 4097), false); assert.equal(c.reserve(f, 4096), true); assert.equal(c.reserve(f, 1), false);
  c.image(f, { stage: 'pass' }, new Uint8Array(4096), true);
  assert.equal(c.report.images[0].data.byteLength, 4096);
  c.failedAttempt(f, 'binding too small'); c.beginAttempt(f, 1);
  assert.equal(c.report.images.length, 0); assert.equal(c.report.failedAttempts.length, 1); assert.equal(c.reserve(f, 4096), true);
});

test('capture pass indices match decoded implicit passes and every skipped pass is disclosed', async () => {
  const env = createValidationDevice(); const c = createVkFrameCapture(); const f = frame(11);
  const original = f.passes[0]; f.passes = [{ ...original, attachments: [], ops: [] }, ...Array.from({ length: 65 }, () => ({ ...original }))];
  c.arm(11); c.batch(f);
  try {
    const r = await createVkRenderer2(env.canvas, { capture: c });
    assert.equal((await r.render(f)).rendered, true);
    const images = c.report.images.filter(i => i.stage === 'pass');
    assert.equal(images.length, 65); assert.equal(images.find(i => i.data).pass, 1);
    assert.deepEqual(images.filter(i => i.skipped).map(i => i.pass), [64, 65]);
    r.destroy();
  } finally { env.restore(); }
});

test('unsupported attachment and colorless terminal stages carry explicit omission evidence', async () => {
  const env = createValidationDevice();
  try {
    const c = createVkFrameCapture(); const f = frame(12);
    f.passes.push({ ...f.passes[0], attachments: [{ format: 0xffff, imageId: 'bad', loadOp: 1 }] });
    c.arm(12); c.batch(f);
    const r = await createVkRenderer2(env.canvas, { capture: c });
    await r.render(f);
    assert.equal(c.report.images.find(i => i.pass === 1)?.skipped, 'unsupported-attachment');
    r.destroy();
    const d = createVkFrameCapture(); const empty = frame(13); empty.passes = [];
    d.arm(13); d.batch(empty);
    const second = await createVkRenderer2(env.canvas, { capture: d });
    await second.render(empty);
    assert.deepEqual(d.report.images.map(i => [i.stage, i.skipped]), [['present-source', 'no-colour-pass'], ['canvas', 'no-colour-pass']]);
    second.destroy();
  } finally { env.restore(); }
});

test('an eager readback before a requested later submission terminates capture loudly', async () => {
  const { installVkPageTier, V2_MAGIC, V2_VERSION, V2_REC } = await import('../../../vkwebgpu.mjs');
  const env = createValidationDevice(); const tier = installVkPageTier({ canvas: env.canvas, log() {} });
  try {
    globalThis.bwVkCapture.arm(42);
    const bytes = new Uint8Array(52), view = new DataView(bytes.buffer);
    view.setUint32(0, V2_MAGIC, true); view.setUint16(4, V2_VERSION, true);
    view.setUint16(16, V2_REC.FRAME_BEGIN, true); view.setUint32(20, 16, true);
    view.setUint32(24, 41, true); view.setUint32(28, 4, true); view.setUint32(32, 4, true);
    view.setUint16(40, V2_REC.FRAME_END, true); view.setUint32(44, 4, true); view.setUint32(48, 41, true);
    await tier.chunk(bytes, 0, { readback: true });
    assert.equal(globalThis.bwVkCapture.report?.status, 'error');
    assert.match(globalThis.bwVkCapture.report.error, /readback/);
  } finally { await tier.destroy(); env.restore(); }
});

test('capture maps each GPU buffer range once, matching WebGPU overlap rules', async () => {
  const env = createValidationDevice(); const create = env.device.createBuffer.bind(env.device);
  env.device.createBuffer = desc => {
    const buffer = create(desc), get = buffer.getMappedRange.bind(buffer); let outstanding = false;
    buffer.getMappedRange = () => { assert.equal(outstanding, false, 'overlapping getMappedRange calls'); outstanding = true; return get(); };
    buffer.unmap = () => { outstanding = false; };
    return buffer;
  };
  const c = createVkFrameCapture(); const f = frame(14); c.arm(14); c.batch(f);
  try {
    const r = await createVkRenderer2(env.canvas, { capture: c }); await r.render(f);
    assert.equal(c.report.images.length, 3); assert.ok(c.report.images.every(i => i.data && !i.error)); r.destroy();
  } finally { env.restore(); }
});
