import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const read = p => readFileSync(resolve(root, p), 'utf8');

// Directive: nothing is dropped silently. These are source-shape guards; the
// behavioural coverage lives in the native bridge harness and the renderer tests.

test('vkwebgpu.mjs counts and logs every unsupported draw through noteUnsupported', () => {
  const src = read('web/vkwebgpu.mjs');
  const bare = [...src.matchAll(/stats\.unsupported\s*(\+\+|\+=)/g)];
  assert.equal(bare.length, 1, 'stats.unsupported may only be incremented inside noteUnsupported');
  assert.match(src, /const noteUnsupported = \(reason, detail\) => \{ stats\.unsupported\+\+; return noteDrop\(reason, detail\); \};/);
  assert.doesNotMatch(src, /default:\s*break;/, 'a replay switch default must report the unknown op');
  assert.match(src, /default: noteUnsupported\('unknown-replay-op'/);
});

test('vk64bridge.cpp guards that discard guest data go through VK64_DROP_IF', () => {
  const src = read('source/vulkan/vk64bridge.cpp');
  assert.ok((src.match(/VK64_DROP_IF\(/g) || []).length >= 200);
  assert.match(src, /#define VK64_DROP_IF\(action, \.\.\.\) do \{ if \(__VA_ARGS__\) \{ vk64DropLog\(#__VA_ARGS__, __LINE__\); action; \} \} while \(0\)/);
  // The pre-existing class of silent guard that hid the Info2L layout bug.
  assert.doesNotMatch(src, /if \([^)]*nreg[^)]*>[^)]*\)\s*return 0;/);
});
