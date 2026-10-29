import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const bridge = readFileSync(resolve(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');

test('swapchain binary semaphores signal on acquire/submit and are consumed by submit/present in both ABIs', () => {
  assert.match(bridge, /vkAcquireNextImageKHR[\s\S]{0,500}s->signalled = 1/);
  assert.ok((bridge.match(/sem->signalled = 0/g) || []).length >= 4,
    '64/32 submit waits and present waits consume binary semaphore signals');
  assert.ok((bridge.match(/sem->signalled = 1/g) || []).length >= 3,
    'queue submits signal binary semaphores in both ABIs');
  assert.ok((bridge.match(/"binary-semaphore-wait"/g) || []).length >= 3);
  assert.equal((bridge.match(/"binary-semaphore-present-wait"/g) || []).length, 2);
  // Since the 2026-10-26 vkd3d disposition, timeline submits are emulated
  // (immediate-execution counter model) instead of rejected; the submit-side
  // contract is pinned in vk-timeline-semaphore.test.mjs.
  assert.doesNotMatch(bridge, /timeline submit is not connected to WebGPU replay/);
  assert.doesNotMatch(bridge, /submit2 timeline signal is not supported/);
});
