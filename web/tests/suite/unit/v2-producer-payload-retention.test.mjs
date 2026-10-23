// web/tests/suite/unit/v2-producer-payload-retention.test.mjs
//
// Regression test for Portal's black menu background (2026-10-22).
//
// v2QueueFrame used to pop complete submissions when the pending chunk count
// exceeded V2_PRODUCER_CHUNK_RING. A dropped submission can carry the only
// copy of buffer/image payloads that later frames reference byte-less; the
// page has no upstream resend channel, so the content stayed missing and
// createBindGroup rejected whole frames. Complete submissions must be kept
// until the present hop drains them.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const src = readFileSync(join(repoRoot, 'source/vulkan/vk64bridge.cpp'), 'utf8');

function queueFrameBody() {
  const m = src.match(/void v2QueueFrame\(V2FrameAsm& fa\) \{([\s\S]*?)\n\}/);
  if (!m) throw new Error('Could not find v2QueueFrame in vk64bridge.cpp');
  return m[1];
}

test('v2QueueFrame keeps every complete submission for the present hop', () => {
  const body = queueFrameBody();
  assert.ok(!body.includes('pop_front'), 'v2QueueFrame must not discard queued submissions');
  assert.ok(body.includes('g_v2Pending.push_back'), 'every completed frame must still be queued');
});

test('v2QueueFrame reports oversize presentations without dropping payloads', () => {
  const body = queueFrameBody();
  assert.ok(body.includes('V2_PRODUCER_CHUNK_RING'), 'the soft cap must still be observable');
  assert.ok(body.includes('keeping all payloads'), 'oversize logging must state payloads are retained');
});
