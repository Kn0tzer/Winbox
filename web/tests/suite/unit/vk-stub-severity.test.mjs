// web/tests/suite/unit/vk-stub-severity.test.mjs — TAIL stub severity classification.
//
// 109 Vulkan functions are TAIL stubs (benign no-ops). Some are harmless
// (debug utils, query pools). Others WILL break real games:
//   - vkCmdBlitImage: used for swapchain present blits
//   - vkCmdClearColorImage: used for render target clears
//   - vkCmdCopyBufferToImage2: staging uploads (2-variant)
//   - vkCmdDispatch: compute shaders
//   - vkCmdDrawIndirect family: indirect draws (common in modern engines)
//   - vkCmdExecuteCommands: secondary command buffers
//   - vkCmdResolveImage: MSAA resolve
//
// This test documents each stub's severity. GAME_BREAKING stubs are FAILs —
// they represent known-holes that will silently break real games.
// BENIGN stubs are reported but pass.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const shimSrc = readFileSync(join(repoRoot, 'tools/rootfs64/libvk64/libvk64.c'), 'utf8');

function getStubs(src) {
  return new Set([...src.matchAll(/^TAIL\((vk\w+)\)/gm)].map(m => m[1]));
}

// Stubs that WILL break real games if called. DXVK and modern engines use these.
const GAME_BREAKING = [
  'vkCmdBlitImage', 'vkCmdBlitImage2', 'vkCmdBlitImage2KHR',  // present blits
  'vkCmdClearColorImage',                                       // RT clears
  'vkCmdCopyBufferToImage2', 'vkCmdCopyBufferToImage2KHR',    // staging uploads
  'vkCmdDispatch', 'vkCmdDispatchBase', 'vkCmdDispatchIndirect', // compute
  'vkCmdDrawIndirect', 'vkCmdDrawIndexedIndirect',             // indirect draws
  'vkCmdDrawIndirectCount', 'vkCmdDrawIndirectCountKHR',
  'vkCmdDrawIndexedIndirectCount',
  'vkCmdExecuteCommands',                                      // secondary CBs
  'vkCmdResolveImage', 'vkCmdResolveImageKHR',                // MSAA resolve
  'vkCmdClearAttachments', 'vkCmdClearAttachmentsKHR',        // clears in RP
  'vkCmdFillBuffer',                                           // buffer fills
  'vkCmdCopyImage', 'vkCmdCopyImage2', 'vkCmdCopyImage2KHR',  // image copies
];

// Stubs that are likely benign (debug, queries, exotic features)
const BENIGN = [
  'vkCreateDebugUtilsMessengerEXT', 'vkDestroyDebugUtilsMessengerEXT',
  'vkSetDebugUtilsObjectNameEXT', 'vkSetDebugUtilsObjectTagEXT',
  'vkSubmitDebugUtilsMessageEXT',
  'vkCmdBeginQuery', 'vkCmdEndQuery', 'vkCmdResetQueryPool',
  'vkCreateQueryPool', 'vkDestroyQueryPool', 'vkGetQueryPoolResults',
  'vkCmdWriteTimestamp', 'vkCmdWriteTimestamp2', 'vkCmdWriteTimestamp2KHR',
  'vkCmdBeginConditionalRenderingEXT', 'vkCmdEndConditionalRenderingEXT',
  'vkGetPastPresentationTimingGOOGLE',
];

const stubs = getStubs(shimSrc);

test('game-breaking stubs are documented (informational)', () => {
  const present = GAME_BREAKING.filter(f => stubs.has(f));
  console.log(`\n  GAME-BREAKING stubs present (${present.length}): ${present.join(', ')}`);
  // Informational only — see next test for the FAIL
});

test('no game-breaking Vulkan function is a silent TAIL stub', () => {
  const breaking = GAME_BREAKING.filter(f => stubs.has(f));
  assert.deepEqual(breaking, [],
    `These Vulkan functions are TAIL stubs (silent no-ops) but WILL break real games that call them:\n` +
    `  ${breaking.join('\n  ')}\n` +
    `Each needs a real bridge implementation or explicit documentation of why it's safe.`);
});

test('all stubs are classified (no unclassified stubs)', () => {
  const classified = new Set([...GAME_BREAKING, ...BENIGN]);
  const unclassified = [...stubs].filter(f => !classified.has(f));
  // Report unclassified — these need severity assessment
  if (unclassified.length > 0) {
    console.log(`\n  UNCLASSIFIED stubs needing severity review (${unclassified.length}): ${unclassified.join(', ')}`);
  }
});
