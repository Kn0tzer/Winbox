// web/tests/suite/unit/vk-shim-gipa.test.mjs — Vulkan shim gipa registration test.
//
// REGRESSION TEST for the D3D11 LIT=0 bug (commit 75a12466).
//
// Root cause: vkCmdCopyBuffer was never added to g_procs[] in the shim.
// vkGetInstanceProcAddr returned the benign TAIL stub, so DXVK's staging
// buffer uploads never executed. Vertex buffers stayed zero-filled → LIT=0.
//
// This test statically verifies that every Vulkan function the bridge
// implements has a corresponding entry in the shim's g_procs[] table,
// and is NOT in the TAIL (benign stub) list.
//
// If this test had existed, the LIT=0 bug would have been caught at
// commit time instead of requiring hours of live-probe debugging.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const shimPath = join(repoRoot, 'tools/rootfs64/libvk64/libvk64.c');
const abiPath = join(repoRoot, 'source/vulkan/vk64bridge_abi.h');

const shimSrc = readFileSync(shimPath, 'utf8');
const abiSrc = readFileSync(abiPath, 'utf8');

// Extract function names from g_procs[] E(name, ...) entries
function getGipaRegistered(src) {
  const registered = new Set();
  const gprocsMatch = src.match(/static const struct procEntry g_procs\[\] = \{([\s\S]*?)\n\};/);
  if (!gprocsMatch) throw new Error('Could not find g_procs[] in shim source');
  const body = gprocsMatch[1];
  for (const m of body.matchAll(/E\((vk\w+),/g)) {
    registered.add(m[1]);
  }
  return registered;
}

// Extract function names from TAIL(...) stub list
function getTailStubs(src) {
  const stubs = new Set();
  for (const m of src.matchAll(/^TAIL\((vk\w+)\)/gm)) {
    stubs.add(m[1]);
  }
  return stubs;
}

// Extract VK64_fn_* IDs from the ABI header that have bridge handlers
// (we check a curated list of critical functions rather than parsing all)
const CRITICAL_FUNCTIONS = [
  // The LIT=0 root cause — DXVK staging uploads
  'vkCmdCopyBuffer',
  'vkCmdCopyBuffer2',
  'vkCmdCopyBuffer2KHR',
  // Readback path
  'vkCmdCopyBufferToImage',
  'vkCmdCopyImageToBuffer',
  'vkCmdCopyImageToBuffer2',
  // Core rendering
  'vkCmdDraw',
  'vkCmdDrawIndexed',
  'vkCmdBindVertexBuffers',
  'vkCmdBindIndexBuffer',
  'vkCreateBuffer',
  'vkCreateImage',
  'vkBindBufferMemory',
  'vkBindImageMemory',
  'vkMapMemory',
  'vkUnmapMemory',
  // Command infrastructure
  'vkBeginCommandBuffer',
  'vkEndCommandBuffer',
  'vkQueueSubmit',
  'vkCreateCommandPool',
  'vkAllocateCommandBuffers',
];

test('critical Vulkan functions are registered in g_procs[] (not TAIL stubs)', () => {
  const registered = getGipaRegistered(shimSrc);
  const stubs = getTailStubs(shimSrc);

  for (const fn of CRITICAL_FUNCTIONS) {
    assert.ok(
      registered.has(fn),
      `${fn} MUST be in g_procs[] — if it's a TAIL stub, vkGetInstanceProcAddr returns a no-op and the guest silently does nothing`
    );
    assert.ok(
      !stubs.has(fn),
      `${fn} MUST NOT be in TAIL() stub list — it's registered in g_procs[]`
    );
  }
});

test('vkCmdCopyBuffer family specifically registered (LIT=0 regression)', () => {
  const registered = getGipaRegistered(shimSrc);
  // This is the exact bug: these three were TAIL stubs, causing zero-filled vertex buffers
  for (const fn of ['vkCmdCopyBuffer', 'vkCmdCopyBuffer2', 'vkCmdCopyBuffer2KHR']) {
    assert.ok(registered.has(fn), `${fn} must be gipa-registered (was TAIL stub in LIT=0 bug)`);
  }
});

test('ABI header has function IDs for registered functions', () => {
  const registered = getGipaRegistered(shimSrc);
  // Every E(name, VK64_fn_name) in g_procs[] must have a VK64_fn_name in the ABI header
  const gprocsMatch = shimSrc.match(/static const struct procEntry g_procs\[\] = \{([\s\S]*?)\n\};/);
  const ids = new Set();
  for (const m of gprocsMatch[1].matchAll(/E\(vk\w+,\s*(VK64_fn_\w+)\)/g)) {
    ids.add(m[1]);
  }
  for (const id of ids) {
    assert.ok(
      abiSrc.includes(id),
      `ABI header must define ${id} (used in g_procs[])`
    );
  }
});
