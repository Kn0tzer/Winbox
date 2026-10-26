// GUARD (silent drop): command buffers without a render pass (copy/blit/clear-only, vkd3d copy
// queues) used to be skipped by the bridge. They now travel as 1x1 frames; the page must run them
// but never let them present (they would resize the canvas to their 1x1 placeholder).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { decodeV2Frame, V2_REC, v2HasColourPass, v2PresentIndex } from '../../../vk-protocol.mjs';

const colourFrame = { passes: [{ attachments: [{ isDepth: false }] }] };
const depthFrame = { passes: [{ attachments: [{ isDepth: true }] }] };
const copyFrame = { passes: [{ attachments: [] }] };

test('v2HasColourPass is true only for a colour attachment', () => {
  assert.equal(v2HasColourPass(colourFrame), true);
  assert.equal(v2HasColourPass(depthFrame), false);
  assert.equal(v2HasColourPass(copyFrame), false);
  assert.equal(v2HasColourPass({ passes: [] }), false);
});

test('v2PresentIndex skips trailing transfer-only submissions', () => {
  assert.equal(v2PresentIndex([colourFrame]), 0);
  assert.equal(v2PresentIndex([colourFrame, copyFrame]), 0);
  assert.equal(v2PresentIndex([colourFrame, depthFrame, copyFrame, colourFrame, copyFrame]), 3);
  assert.equal(v2PresentIndex([copyFrame, copyFrame]), 0, 'no colour target anywhere: render() then declines to present each one');
});

test('a 1x1 placeholder frame carrying only an image copy decodes into an attachment-less pass', () => {
  const header = new Uint8Array(16), end = new Uint8Array(4), copy = new Uint8Array(68);
  const h = new DataView(header.buffer); h.setUint32(4, 1, true); h.setUint32(8, 1, true);
  const c = new DataView(copy.buffer); c.setBigUint64(0, 1n, true); c.setBigUint64(8, 2n, true);
  c.setUint32(56, 4, true); c.setUint32(60, 4, true); c.setUint32(64, 1, true);
  const frame = decodeV2Frame([{ type: V2_REC.FRAME_BEGIN, payload: header },
    { type: V2_REC.IMAGE_COPY, payload: copy }, { type: V2_REC.FRAME_END, payload: end }]);
  assert.equal(frame.width, 1);
  assert.equal(frame.passes.length, 1);
  assert.equal(frame.passes[0].ops[0].op, 'imageCopy');
  assert.equal(v2HasColourPass(frame), false);
});

test('the page render() presents only through v2PresentIndex / v2HasColourPass', () => {
  const src = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.match(src, /v2PresentIndex\(frame\.submissions\)/);
  assert.match(src, /if \(present && !v2HasColourPass\(frame\)\) present = false;/);
});
