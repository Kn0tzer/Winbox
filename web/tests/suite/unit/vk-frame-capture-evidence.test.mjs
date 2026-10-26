// completeEvidence pins for the same-frame capture tool (vk-frame-capture).
// The capture is the gate oracle for present correctness, so its completeness
// verdict itself is pinned here: one positive case and one test per veto
// condition in finish() — guestPresent sideband present, wire non-empty,
// zero omitted bytes in each of the three budgets (wire/state/image), no
// skipped or errored image, at least one canvas and one present-source
// image, no recorded issues, and no finish error.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createVkFrameCapture } from '../../../vk-frame-capture.mjs';

const PRESENT = { swapchains: [{ swapchain: '3', index: 0, image: '170', w: 4, h: 4 }], lastSubmission: 7 };

function run({ maxBytes = 64 * 1024 * 1024, isBoundary, wireChunks = [[1, 2, 3]], present = PRESENT,
               frameExtra = {}, images = null, finishError = undefined } = {}) {
  const cap = createVkFrameCapture({ maxBytes, ...(isBoundary ? { isBoundary } : {}) });
  cap.arm();
  for (const c of wireChunks) cap.wire(new Uint8Array(c), 0);
  const frame = { frameNo: 7, ...frameExtra };
  assert.equal(cap.batch(frame, present), true, 'batch should select the frame');
  const imgs = images ?? [
    { meta: { stage: 'canvas' }, bytes: new Uint8Array(4) },
    { meta: { stage: 'present-source', imageId: '00000000000000aa' }, bytes: new Uint8Array(4) },
  ];
  for (const im of imgs) cap.image(frame, im.meta, im.bytes);
  cap.finish(frame, finishError);
  return cap.report;
}

test('complete evidence: all conditions satisfied', () => {
  const r = run({});
  assert.equal(r.status, 'complete');
  assert.equal(r.completeEvidence, true);
  // The present-source entry carries the renderer's selected imageId, so a
  // guest-selected vs page-presented comparison is by identity, not pixels.
  const ps = r.images.find(i => i.stage === 'present-source');
  assert.equal(ps.imageId, '00000000000000aa');
});

test('veto: guestPresent sideband missing', () => {
  const r = run({ present: null });
  assert.equal(r.completeEvidence, false);
});

test('veto: wire empty', () => {
  const r = run({ wireChunks: [] });
  assert.equal(r.completeEvidence, false);
});

test('veto: omitted wire bytes (chunk over budget dropped)', () => {
  const r = run({ maxBytes: 64, wireChunks: [[1, 2, 3], new Array(100).fill(9)] });
  assert.ok(r.omittedWireBytes > 0);
  assert.equal(r.completeEvidence, false);
});

test('veto: omitted state bytes (frame state over budget)', () => {
  const r = run({
    maxBytes: 256,
    frameExtra: { blob: new Uint8Array(1024) },
    images: [
      { meta: { stage: 'canvas' }, bytes: null },
      { meta: { stage: 'present-source' }, bytes: null },
    ],
  });
  assert.ok(r.omittedStateBytes > 0);
  assert.equal(r.omittedImageBytes || 0, 0, 'state veto must be isolated');
  assert.equal(r.completeEvidence, false);
});

test('veto: omitted image bytes (image over remaining budget)', () => {
  const r = run({
    maxBytes: 256,
    images: [
      { meta: { stage: 'canvas' }, bytes: new Uint8Array(4) },
      { meta: { stage: 'present-source' }, bytes: new Uint8Array(4096) },
    ],
  });
  assert.ok((r.omittedImageBytes || 0) > 0);
  assert.equal(r.completeEvidence, false);
});

test('veto: a skipped image', () => {
  const r = run({
    images: [
      { meta: { stage: 'canvas', skipped: 'format' }, bytes: null },
      { meta: { stage: 'present-source' }, bytes: new Uint8Array(4) },
    ],
  });
  assert.equal(r.completeEvidence, false);
});

test('veto: an errored image', () => {
  const r = run({
    images: [
      { meta: { stage: 'canvas' }, bytes: new Uint8Array(4) },
      { meta: { stage: 'present-source', error: 'map failed' }, bytes: null },
    ],
  });
  assert.equal(r.completeEvidence, false);
});

test('veto: no canvas image', () => {
  const r = run({ images: [{ meta: { stage: 'present-source' }, bytes: new Uint8Array(4) }] });
  assert.equal(r.completeEvidence, false);
});

test('veto: no present-source image', () => {
  const r = run({ images: [{ meta: { stage: 'canvas' }, bytes: new Uint8Array(4) }] });
  assert.equal(r.completeEvidence, false);
});

test('veto: capture armed after the boundary (recorded issue)', () => {
  const r = run({ isBoundary: () => false });
  assert.equal(r.status, 'error');
  assert.equal(r.completeEvidence, false);
});

test('veto: finish called with an error', () => {
  const r = run({ finishError: 'render failed' });
  assert.equal(r.status, 'error');
  assert.equal(r.completeEvidence, false);
});

test('renderer stamps the selected imageId on present-source capture entries', () => {
  const page = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.match(page, /captureTexture\(presentTex, 'present-source', \{ imageId: presentImageSel \}\)/);
  assert.match(page, /presentImageSel = a\.imageId/);
  assert.match(page, /presentImageSel = fallbackPresent\.imageId/);
});
