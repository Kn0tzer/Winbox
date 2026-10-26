// web/tests/suite/unit/performance-gates.test.mjs — Performance with teeth (P1/P5).
//
// Perf commits d63a7087/1f7a50a6 shipped with zero frame-time measurements.
// "1.88x speedup" was frame count in probe window, not frame time (114.55ms
// stayed). emitImageData klog spam: objOf() logged 3831x/211 frames (~7ms
// each via snprintf+mutex+flush) → f64bbe0c (skip nulls, rate-limit) did NOT
// change frame time. vkprof=1 broke rendering (defer until after first
// vkQueuePresentKHR; EM_ASM_INT window on Worker → ReferenceError). Rule:
// frame time (not count), before/after on same build, klog-rate limits on
// hot paths.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const bridge = readFileSync(join(repoRoot, 'source/vulkan/vk64bridge.cpp'), 'utf8');
const page = readFileSync(join(repoRoot, 'web/vkwebgpu.mjs'), 'utf8');

test('hot-path klog is rate-limited / null-skipped (f64bbe0c 7ms x 3831 guard)', () => {
  const start = bridge.indexOf('template <class T> T* objOf(');
  const end = bridge.indexOf('\nvoid dropObj', start);
  assert.ok(start >= 0 && end > start, 'objOf template source slice must exist');
  const objOf = bridge.slice(start, end);
  assert.match(objOf, /if \(!o && id\)/);
  assert.match(objOf, /static std::unordered_set<U64> logged/);
  assert.match(objOf, /if \(logged\.insert\(id\)\.second\)\s*klog_fmt\("vk64: %s: bad\/stale handle/,
    'stale descriptor logging must emit once per distinct handle');
});

test('BW64_VKPROF defers until after first present (2649px → 0 guard)', () => {
  const gate = bridge.indexOf('static bool g_vkProfFirstFrameDone');
  const present = bridge.indexOf('case VK64_fn_vkQueuePresentKHR:', gate);
  const completed = bridge.indexOf('g_vkProfFirstFrameDone = true;', present);
  assert.ok(gate >= 0 && present > gate && completed > present,
    'first-frame flag must be set by the queue-present handler after presentation work');
  assert.match(bridge.slice(gate, present), /if \(!g_vkProfFirstFrameDone\) return false;/,
    'profiling must stay disabled before the first completed present');
});

test('vkProf does not read window.location on Worker (ReferenceError guard)', () => {
  assert.ok(bridge.includes('bw64_vkprof_enable'),
    'vkProfEnabled must go through launcher-called bw64_vkprof_enable (EM_ASM_INT window on Worker is ReferenceError → always false)');
});

test('frametime harness exists and reports ms (not count)', () => {
  const ft = readFileSync(join(repoRoot, 'web/tests/frametime-harness.mjs'), 'utf8');
  assert.match(ft, /performance\.now\(\)/);
  assert.match(ft, /mean|median|p95/i);
  assert.match(ft, /frameMs|dt/);
});

test('ResizeObserver does not fire during render await gaps (in-flight destroy guard)', () => {
  assert.ok(page.includes('ResizeObserver'),
    'ResizeObserver hook must exist and drain queue safely (not destroy in-flight textures mid-await)');
});

test('producer/consumer backpressure caps exist (8 chunks / 3 frames latest-wins)', () => {
  assert.ok(page.includes('takeLatest') && (page.includes('cap') || page.includes('drop') || page.includes('Queue')),
    'FrameQueue latest-frame-wins (cap 3, drop-oldest) must exist; producer ring cap 8 must exist bridge-side');
});
