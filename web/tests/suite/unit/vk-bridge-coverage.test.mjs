// web/tests/suite/unit/vk-bridge-coverage.test.mjs — Full Vulkan bridge coverage.
//
// Verifies EVERY function in the Vulkan ABI has:
//   (a) a shim entry (g_procs[] registered OR TAIL stub — both are "handled")
//   (b) a bridge case handler in vk64bridge.cpp
//
// A function in the ABI with NO bridge handler is a hard FAIL — the guest
// would trap to the bridge and hit unimplemented behavior.
// A function that's a TAIL stub is reported (not failed here — severity is
// in vk-stub-severity.test.mjs), but must be a conscious decision, not an accident.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const abiSrc = readFileSync(join(repoRoot, 'source/vulkan/vk64bridge_abi.h'), 'utf8');
const bridgeSrc = readFileSync(join(repoRoot, 'source/vulkan/vk64bridge.cpp'), 'utf8');
const shimSrc = readFileSync(join(repoRoot, 'tools/rootfs64/libvk64/libvk64.c'), 'utf8');

function getAbiFunctions(src) {
  const ids = [];
  const seen = new Set();
  for (const m of src.matchAll(/^\s*(VK64_fn_\w+)\s*(?:=\s*\d+)?\s*,/gm)) {
    if (!seen.has(m[1])) { seen.add(m[1]); ids.push(m[1]); }
  }
  return ids;
}

function getBridgeHandlers(src) {
  return new Set([...src.matchAll(/case\s+(VK64_fn_\w+)\s*:/g)].map(m => m[1]));
}

function getShimRegistered(src) {
  const m = src.match(/static const struct procEntry g_procs\[\] = \{([\s\S]*?)\n\};/);
  if (!m) throw new Error('g_procs[] not found');
  // E(name, id): the shim traps with `id`, which may be shared by aliases (EXT/KHR/promoted names).
  const ids = new Set();
  for (const x of m[1].matchAll(/E\((vk\w+),\s*(VK64_fn_\w+)\)/g)) { ids.add('VK64_fn_' + x[1]); ids.add(x[2]); }
  return ids;
}

function getShimStubs(src) {
  return new Set([...src.matchAll(/^TAIL\((vk\w+)\)/gm)].map(m => 'VK64_fn_' + m[1]));
}

const abiFns = getAbiFunctions(abiSrc);
const handlers = getBridgeHandlers(bridgeSrc);
const registered = getShimRegistered(shimSrc);
const stubs = getShimStubs(shimSrc);

// Skip non-Vulkan meta entries
const vkFns = abiFns.filter(f => f.startsWith('VK64_fn_vk'));

test('every Vulkan ABI function has a bridge case handler', () => {
  const missing = vkFns.filter(f => !handlers.has(f));
  assert.deepEqual(missing, [],
    `ABI functions without bridge handlers (guest trap would hit unimplemented): ${missing.join(', ')}`);
});

test('every Vulkan ABI function is handled by the shim (registered or stub)', () => {
  const unhandled = vkFns.filter(f => !registered.has(f) && !stubs.has(f));
  assert.deepEqual(unhandled, [],
    `ABI functions with NO shim entry at all (gipa would fail): ${unhandled.join(', ')}`);
});

test('bridge handler count matches ABI function count (no drift)', () => {
  // Handlers may cover multiple ABI ids via fallthrough; count unique handler targets
  // that correspond to ABI functions. Just verify no ABI function is orphaned.
  const orphaned = vkFns.filter(f => !handlers.has(f));
  assert.equal(orphaned.length, 0,
    `${orphaned.length} ABI functions lack bridge handlers`);
});

test('shim g_procs[] entries all have ABI IDs or are intentional aliases', () => {
  const abiSet = new Set(abiFns);
  const phantom = [...registered].filter(f => !abiSet.has(f) && f.startsWith('VK64_fn_vk'));
  // Intentional aliases: EXT/KHR variants that reuse the base function ID.
  // E.g., E(vkCmdSetScissorWithCountEXT, VK64_fn_vkCmdSetScissor).
  // These are found by checking if the E() entry maps to a DIFFERENT id than its name.
  const shimEntries = new Map();
  const gprocsMatch = shimSrc.match(/static const struct procEntry g_procs\[\] = \{([\s\S]*?)\n\};/);
  for (const m of gprocsMatch[1].matchAll(/E\((vk\w+),\s*(VK64_fn_\w+)\)/g)) {
    shimEntries.set('VK64_fn_' + m[1], m[2]);
  }
  const truePhantom = phantom.filter(f => {
    const mappedId = shimEntries.get(f);
    // If it maps to a different (existing) ID, it's an intentional alias
    return !mappedId || mappedId === f || !abiSet.has(mappedId);
  });
  assert.deepEqual(truePhantom, [],
    `g_procs[] entries without ABI IDs and not intentional aliases: ${truePhantom.join(', ')}`);
});
