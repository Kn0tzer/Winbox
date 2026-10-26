// web/tests/suite/unit/frame-metrics-contract.test.mjs — writer/reader contract.
//
// The fixture web/tests/fixtures/frame-metrics-snapshot.txt is the VERBATIM
// output of source/util/bw64metrics.cpp (compiled natively by
// tools/bw64metrics-fixture.sh), i.e. the exact line a running program prints.
// This test parses it with the panel's own reader and checks that every stage
// the panel promises to show is present, classifiable and correctly labelled.
//
// So: if the C++ writer renames or drops a field, this fails here; if the reader
// starts expecting something the writer does not produce, it fails here too.
//
// Regenerate after changing the registry:
//   tools/bw64metrics-fixture.sh

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { STATE, createMetricsModel, parseMetricsLine } from '../../../frame-metrics.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const fixturePath = join(repoRoot, 'web/tests/fixtures/frame-metrics-snapshot.txt');
const line = readFileSync(fixturePath, 'utf8').trim();

// Field lists the panel reads. Kept next to the test so a schema change has to
// touch exactly one place per side.
const REQUIRED_TOP = ['v', 'uptimeMs', 'winMs', 'arch', 'enabled', 'present', 'vk', 'vk32', 'stages', 'jit', 'unavailable'];
const REQUIRED_PRESENT = ['vk', 'vk32', 'sdl', 'count', 'fps', 'frameMs', 'path'];
const REQUIRED_VK = ['supported', 'frames', 'wallMs', 'submitMs', 'trapOtherMs', 'presentMs', 'hopMs',
  'bufMs', 'imgMs', 'guestMs', 'trapCalls', 'hopChunks', 'hopBytes'];
const REQUIRED_VK32 = ['supported', 'frames', 'wallMs', 'breakdown', 'reason'];
const REQUIRED_STAGE = ['supported', 'calls', 'ms'];
const REQUIRED_JIT = ['supported', 'available', 'engine', 'enabled', 'blocksEmitted', 'blocksInstantiated',
  'blocksEntered', 'instructionsRetired', 'fallbacks', 'emitUs', 'instantiateUs'];

test('the runtime writer still emits every field the panel reads', () => {
  const snapshot = parseMetricsLine(line);
  assert.ok(snapshot, 'the fixture line must parse as a v1 snapshot');
  for (const key of REQUIRED_TOP) assert.ok(key in snapshot, 'missing top-level ' + key);
  for (const key of REQUIRED_PRESENT) assert.ok(key in snapshot.present, 'missing present.' + key);
  for (const key of REQUIRED_VK) assert.ok(key in snapshot.vk, 'missing vk.' + key);
  for (const key of REQUIRED_VK32) assert.ok(key in snapshot.vk32, 'missing vk32.' + key);
  for (const stage of ['syscall', 'gl', 'audio']) {
    for (const key of REQUIRED_STAGE) assert.ok(key in snapshot.stages[stage], 'missing stages.' + stage + '.' + key);
  }
  for (const arch of ['cpu32', 'cpu64']) {
    for (const key of REQUIRED_JIT) assert.ok(key in snapshot.jit[arch], 'missing jit.' + arch + '.' + key);
  }
  assert.ok(Array.isArray(snapshot.unavailable) && snapshot.unavailable.length >= 3);
  for (const entry of snapshot.unavailable) {
    assert.equal(typeof entry.id, 'string');
    assert.equal(typeof entry.reason, 'string');
  }
});

test('the fixture reports a real window: fps and frame ms come from a measured span', () => {
  const snapshot = parseMetricsLine(line);
  assert.ok(snapshot.winMs >= 100, 'the fixture window is a genuine >=100 ms span, got ' + snapshot.winMs);
  assert.ok(snapshot.present.fps > 0, 'fps is a positive measured rate');
  assert.ok(Math.abs(snapshot.present.fps - snapshot.present.count * 1000 / snapshot.winMs) < 0.2);
  assert.ok(snapshot.vk.guestMs > 0 && snapshot.vk.submitMs > 0, 'stage spans are real numbers');
  assert.ok(snapshot.vk.guestMs > snapshot.vk.submitMs, 'the guest span dwarfs the bridge spans in this fixture');
});

test('the panel aggregates the real runtime line end to end', () => {
  const snapshot = parseMetricsLine(line);
  const model = createMetricsModel();
  model.setRunState('running');
  model.applyWasm(snapshot, 0);
  const view = model.view(1000);
  assert.equal(view.stale, false);
  const rows = new Map();
  for (const group of view.groups) for (const row of group.rows) rows.set(row.id, row);

  // Every non-guest-layer stage must have a real state, never undefined.
  for (const group of view.groups) {
    for (const row of group.rows) {
      assert.ok([STATE.MEASURED, STATE.INACTIVE, STATE.UNAVAILABLE, STATE.STALE].includes(row.state),
        'row ' + row.id + ' has an invalid state: ' + row.state);
      assert.equal(typeof row.value, 'string');
      assert.equal(typeof row.note, 'string');
      assert.notEqual(row.value, 'null');
      assert.notEqual(row.value, 'undefined');
    }
  }
  assert.equal(rows.get('vkSubmit').value, '0.90 ms');
  assert.equal(rows.get('guestSpan').value, '14.4 ms');
  assert.equal(rows.get('audio').value, '2.00 ms');
  assert.equal(rows.get('gl').value, '2.25 ms');
  assert.equal(rows.get('jit-cpu64').value, 'running \u00b7 emitted-wasm');
  assert.equal(rows.get('jit-cpu32').state, STATE.UNAVAILABLE);
  assert.equal(rows.get('vulkan32').state, STATE.INACTIVE, 'no 32-bit presents in this window');
  assert.match(rows.get('vulkan32').note, /no per-trap timers|no 32-bit guest presented/);
});

test('the runtime build flags the panel depends on are wired in this worktree', () => {
  const registry = readFileSync(join(repoRoot, 'source/util/bw64metrics.cpp'), 'utf8');
  const header = readFileSync(join(repoRoot, 'include/bw64metrics.h'), 'utf8');
  const jitHelper = readFileSync(join(repoRoot, 'include/bw64jitstats.h'), 'utf8');
  // Build-flag reporting must stay explicit for the three layers.
  assert.match(registry, /BOXEDWINE_VULKAN64/);
  assert.match(registry, /BOXEDWINE_OPENGL/);
  assert.match(registry, /BW64_METRICS_CPU64_SUPPORTED/);
  assert.match(registry, /arch\s*==\s*BW64_METRICS_ARCH32/,
    'the metrics implementation must retain a distinct 32-bit branch');
  // The published ABI the JIT lanes implement.
  assert.match(header, /BW64_METRICS_ARCH32 32/);
  assert.match(header, /BW64_METRICS_ARCH64 64/);
  assert.match(header, /void bw64_jit_stats_publish\(int arch, const Bw64JitStats\* stats\);/);
  assert.match(jitHelper, /add_block_entered/);
  assert.match(jitHelper, /add_instructions/);
  assert.match(jitHelper, /set_state\(int arch, bool available, bool enabled, const char\* engine\)/);
});
