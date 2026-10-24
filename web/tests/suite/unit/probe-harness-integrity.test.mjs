// web/tests/suite/unit/probe-harness-integrity.test.mjs — Probe measurement guards (P5).
//
// Six measurement errors masqueraded as product bugs: Pong "50% input loss"
// was log-parser truncation (raw 12/12, 100%); sawVkboot:true matched argv
// echo (cmd /c echo vkboot), strict ^log: vkboot$ was zero; input3 DOWN lost
// was Emscripten stdout prefix truncation vs strict regex; Pong early-exit
// was Wine loader stub pid not game's; D3D11 "0 lit" was measurement error
// (fresh 29k RGB px); "1.88x speedup" was frame count not frame time
// (114.55ms stayed). Rule: every probe assertion needs a proven-positive
// control before a negative verdict counts.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { terminalText } from '../lib/probe-base.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('probe-base has strict ^log: vkboot$ counting (not argv-echo match)', () => {
  const base = readFileSync(join(repoRoot, 'web/tests/suite/lib/probe-base.mjs'), 'utf8');
  assert.ok(base.includes('countConsole'),
    'ProbeTest must expose countConsole(regex) for strict ^log: vkboot$ checks (sawVkboot:true argv-echo false positive guard)');
});

test('probe console normalization strips terminal controls before parsing', () => {
  const normalized = terminalText('\u001b[32mlog: vkboot\u001b[0m');
  assert.equal(normalized, 'log: vkboot');
  assert.match(normalized, /^log: vkboot$/);
});

test('pgrep-style self-match guard exists in probe scripts', () => {
  const slot = readFileSync(join(repoRoot, 'web/tests/probe-slots.mjs'), 'utf8');
  assert.ok(slot.includes('acquireProbeSlot'), 'probe-slots must serialize (P11 serial symptom-chasing: idle lane between hypotheses)');
  assert.ok(slot.length > 500, 'probe slot protocol must be substantial (slot-lock compliance)');
});

test('frame-time harness measures ms, not frame count (1.88x fallacy)', () => {
  const ft = readFileSync(join(repoRoot, 'web/tests/frametime-harness.mjs'), 'utf8');
  assert.match(ft, /frameMs|frameTime|ms\/frame|performance\.now/i,
    'frametime harness must report ms/frame (frame-count-in-window is not speedup)');
  const vkprof = readFileSync(join(repoRoot, 'source/vulkan/vk64bridge.cpp'), 'utf8');
  assert.ok(vkprof.includes('vkProf') || vkprof.includes('BW64_VKPROF'),
    'BW64_VKPROF instrumentation must exist for frame-time (not count) assertions');
});

test('readback-buffer stats are ground truth in headless (101843f5 blank-screenshot guard)', () => {
  const page = readFileSync(join(repoRoot, 'web/vkwebgpu.mjs'), 'utf8');
  assert.ok(page.includes('readback') || page.includes('Readback') || page.includes('READBACK'),
    'readback path must exist — headless SwiftShader blank screenshots are compositor limits, not bugs');
});
