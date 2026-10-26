// Present identity (register row: actual swapchain selection). The guest's
// vkQueuePresentKHR names the swapchain image it presents; the page must
// present the submission that rendered into THAT image, not the last colour
// pass of the last submission (the old heuristic, kept as the fallback when
// no identity is available or nothing matches).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { v2PresentIndex, v2PresentIndexForImage } from '../../../vk-protocol.mjs';

const sub = imageIds => ({
  passes: [{ attachments: imageIds.map(id => ({ imageId: id, isDepth: 0 })) }],
});

test('the submission that rendered the presented image wins over recency', () => {
  const subs = [sub(['aa']), sub(['bb']), sub(['cc'])];
  assert.equal(v2PresentIndex(subs), 2); // heuristic: last colour submission
  assert.equal(v2PresentIndexForImage(subs, 'aa'), 0);
  assert.equal(v2PresentIndexForImage(subs, 'bb'), 1);
  assert.equal(v2PresentIndexForImage(subs, 'cc'), 2);
});

test('no match or no identity falls back (-1)', () => {
  const subs = [sub(['aa']), sub(['bb'])];
  assert.equal(v2PresentIndexForImage(subs, 'zz'), -1);
  assert.equal(v2PresentIndexForImage(subs, null), -1);
  assert.equal(v2PresentIndexForImage(subs, undefined), -1);
  assert.equal(v2PresentIndexForImage([], 'aa'), -1);
});

test('depth attachments and later re-renders are handled', () => {
  const subs = [
    { passes: [{ attachments: [{ imageId: 'aa', isDepth: 1 }] }] }, // depth-only use of the id
    sub(['aa']),
    sub(['aa', 'bb']),
  ];
  assert.equal(v2PresentIndexForImage(subs, 'aa'), 2); // last colour render into it
  const depthOnly = [{ passes: [{ attachments: [{ imageId: 'aa', isDepth: 1 }] }] }];
  assert.equal(v2PresentIndexForImage(depthOnly, 'aa'), -1);
});

test('synchronous readback drains every queued frame in order (hopV2LastToPage)', () => {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const at = bridge.indexOf('static void hopV2LastToPage()');
  assert.ok(at >= 0);
  const body = bridge.slice(at, bridge.indexOf('\n}\n', at));
  assert.match(body, /for \(const auto& f : g_v2Pending\)/);
  assert.ok(!body.includes('g_v2Pending.back()'), 'the prefix-dropping last-frame-only send must stay gone');
  assert.match(body, /\{ readback: true \}/);
});

test('page tier wiring carries the identity from chunk metadata to render', () => {
  const page = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.match(page, /presented\.presentImageId = v2Hex64\(BigInt\(gpImage\)\)/);
  assert.match(page, /v2PresentIndexForImage\(frame\.submissions, frame\.presentImageId\)/);
  assert.match(page, /if \(!presentImageId \|\| String\(a\.imageId\) === presentImageId\)/);
  // the heuristic fallback survives for batches without identity
  assert.match(page, /byImage >= 0 \? byImage : v2PresentIndex\(frame\.submissions\)/);
  assert.match(page, /fallbackPresent/);
});
