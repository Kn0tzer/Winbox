import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const bridge = readFileSync(resolve(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');

test('timeline waits never invent a signal value to satisfy a future wait', () => {
  const begin = bridge.indexOf('case VK64_fn_vkWaitSemaphores:');
  const end = bridge.indexOf('case VK64_fn_vkSignalSemaphore:', begin);
  const wait64 = bridge.slice(begin, end);
  assert.match(wait64, /const bool reached = s->counter >= want/);
  assert.match(wait64, /return \(\(wi\.flags & VK_SEMAPHORE_WAIT_ANY_BIT\) \? any : all\) \? VK_SUCCESS : VK_TIMEOUT/);
  assert.doesNotMatch(wait64, /s->counter\s*=\s*want/);

  const wait32Begin = bridge.indexOf('case VK64_fn_vkWaitSemaphores:', end);
  const wait32End = bridge.indexOf('// ================= C2. Allocation', wait32Begin);
  const wait32 = bridge.slice(wait32Begin, wait32End);
  assert.match(wait32, /return \(\(wi\.flags & VK_SEMAPHORE_WAIT_ANY_BIT\) \? any : all\) \? VK_SUCCESS : VK_TIMEOUT/);
  assert.doesNotMatch(wait32, /counter\s*=\s*.*pValues/);
  assert.match(wait64, /wi\.semaphoreCount && \(!wi\.pSemaphores \|\| !wi\.pValues\)/);
  assert.match(wait32, /wi\.semaphoreCount && \(!wi\.pSemaphores \|\| !wi\.pValues\)/);
  assert.doesNotMatch(bridge, /counter\s*<\s*(?:want|w\.value)\)\s*\w+->counter\s*=/,
    'queue wait handling must not fabricate a semaphore signal');
});
