// web/tests/suite/unit/frame-metrics.test.mjs — live frame-stage metrics.
//
// Covers the parts of the panel that must be right for the numbers to mean
// anything: distinguishing measured / inactive / unavailable / stale, never
// rendering `null` as 0, never summing overlapping stages, labelling the guest
// span as a wall span (not OS CPU) and never presenting JS submit time as GPU
// execution time. The DOM is a 40-line stub, so this runs in plain node:test.
//
// Contract: docs/reports/winbox-metrics-contract-20261018.md
// Aggregation + panel: web/frame-metrics.mjs
// Registry (source of the JSON): source/util/bw64metrics.cpp (+ its native
// selftest tools/bw64metrics-selftest.cpp, which checks the writer side).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  STATE, createMetricsModel, createMetricsPanel, installFrameMetrics,
  parseMetricsLine, formatMs, formatCount, formatFps, formatBytes, formatAge,
} from '../../../frame-metrics.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

// ---------------------------------------------------------------------------
// Minimal DOM stub: enough for createElement / append / textContent / dataset.
// ---------------------------------------------------------------------------
function makeDom() {
  let nextId = 1;
  function makeElement(tag) {
    const node = {
      tagName: String(tag).toUpperCase(),
      id: nextId++,
      children: [],
      attributes: {},
      dataset: {},
      parent: null,
      hidden: false,
      textContent: '',
      get className() { return this._className || ''; },
      set className(value) { this._className = value; },
      classList: { add() {}, remove() {}, toggle() {} },
      setAttribute(name, value) { this.attributes[name] = String(value); },
      getAttribute(name) { return this.attributes[name]; },
      append(...items) {
        for (const item of items) {
          const at = this.children.indexOf(item);
          if (at >= 0) this.children.splice(at, 1);
          item.parent = this;
          this.children.push(item);
        }
      },
      remove() {
        if (this.parent) {
          const at = this.parent.children.indexOf(this);
          if (at >= 0) this.parent.children.splice(at, 1);
        }
        this.parent = null;
      },
      querySelectorAll() { return []; },
    };
    return node;
  }
  return { createElement: makeElement };
}

function walk(node, out = []) {
  out.push(node);
  for (const child of node.children || []) walk(child, out);
  return out;
}
/** All row states keyed by the row's <dt> label text. */
function rowsByLabel(root) {
  const map = new Map();
  for (const node of walk(root)) {
    if (node.tagName !== 'DIV' || node.className !== 'metrics-row') continue;
    const label = node.children[0]?.textContent || '';
    const valueBox = node.children[1];
    map.set(label, {
      value: valueBox?.children?.[0]?.textContent ?? '',
      unit: valueBox?.children?.[1]?.textContent ?? '',
      note: node.children[2]?.textContent ?? '',
      state: node.dataset.state,
      node,
    });
  }
  return map;
}
function textOf(root) { return walk(root).map(n => n.textContent).join(' | '); }

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------
const WASM_FULL = {
  v: 1, uptimeMs: 41230, winMs: 503, arch: 'x86-64', enabled: true,
  present: { vk: 30, vk32: 0, sdl: 0, count: 30, fps: 59.6, frameMs: 16.8, path: 'vk' },
  vk: {
    supported: true, frames: 30, wallMs: 512.0, submitMs: 9.3, trapOtherMs: 12.1,
    presentMs: 3.6, hopMs: 4.2, bufMs: 2.1, imgMs: 5.0, guestMs: 482.8,
    trapCalls: 912, hopChunks: 120, hopBytes: 8123456,
  },
  vk32: { supported: true, frames: 0, wallMs: 0, breakdown: false, reason: '32-bit guest Vulkan presents have no per-trap timers' },
  stages: {
    syscall: { supported: true, calls: 1203, ms: 18.4 },
    gl: { supported: true, calls: 0, ms: 0 },
    audio: { supported: true, calls: 22, ms: 1.7, bytes: 65536 },
  },
  jit: {
    cpu32: { supported: false, available: false, engine: null, enabled: false, blocksEmitted: 0, blocksInstantiated: 0, blocksEntered: 0, instructionsRetired: 0, fallbacks: 0, emitUs: 0, instantiateUs: 0 },
    cpu64: {
      supported: true, available: true, engine: 'emitted-wasm', enabled: true,
      blocksEmitted: 912, blocksInstantiated: 904, blocksEntered: 33210,
      instructionsRetired: 81234567, fallbacks: 12, emitUs: 8120, instantiateUs: 2450,
    },
  },
  unavailable: [
    { id: 'wine', reason: 'guest Win32/ELF code runs inside the emulated CPU; no host boundary' },
    { id: 'dxvk', reason: 'guest-side D3D translation; only its Vulkan calls are host-visible' },
    { id: 'vkd3d', reason: 'guest-side D3D12 translation; no host-visible stage boundary' },
  ],
};

const PAGE_FULL = {
  v1: null,
  v2: {
    presents: 60, windowMs: 1000.5, fps: 59.97, frameMs: 16.68,
    gpu: { status: 'measured', reason: null, ms: 4.25, lastMs: 4.1, samples: 16, zeroResolutions: 0 },
    translate: { ms: 128.0, count: 7 },
    pipeline: { ms: 96.0, count: 4 },
    submit: { ms: 12.5, count: 60 },
    render: { ms: 210.0, count: 60 },
    skipped: 1, unsupported: 2,
  },
  queue: { size: 0, dropped: 0 },
  received: { chunks: 720, frames: 60 },
  rendered: { v1: 0, v2: 60 },
  rejected: 0,
};

const at = (t) => t;

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------
test('parseMetricsLine accepts the runtime line and rejects everything else', () => {
  const line = 'bw64: METRICS ' + JSON.stringify(WASM_FULL);
  assert.deepEqual(parseMetricsLine(line), WASM_FULL);
  assert.deepEqual(parseMetricsLine('[INFO][cpu] ' + line + '\n'), WASM_FULL, 'multi-line stdout hop');
  assert.equal(parseMetricsLine('wine: starting notepad'), null);
  assert.equal(parseMetricsLine('bw64: METRICS {truncated'), null, 'damaged line must not throw');
  assert.equal(parseMetricsLine('bw64: METRICS {"v":2}'), null, 'unknown schema version');
  assert.equal(parseMetricsLine(null), null);
});

// ---------------------------------------------------------------------------
// Status classification
// ---------------------------------------------------------------------------
test('measured / inactive / unavailable are distinct, and null is never zero', () => {
  const model = createMetricsModel();
  model.setRunState('running');
  model.applyWasm(WASM_FULL, at(1000));
  model.applyPage(PAGE_FULL, at(1000));
  const view = model.view(at(1500));
  const rows = new Map();
  for (const group of view.groups) for (const row of group.rows) rows.set(row.id, row);

  assert.equal(rows.get('fps').value, '60.0', 'displayed FPS comes from the page tier');
  assert.equal(rows.get('fps').state, STATE.MEASURED);
  assert.equal(rows.get('gpuExec').value, '4.25 ms');
  assert.equal(rows.get('gpuExec').state, STATE.MEASURED);
  assert.match(rows.get('gpuExec').note, /timestamp/i);

  // No GL traps and no audio in the window: inactive, not "0 ms measured".
  assert.equal(rows.get('gl').state, STATE.INACTIVE, 'supported but silent stage is inactive');
  assert.equal(rows.get('audio').state, STATE.MEASURED);

  // Wine/DXVK/VKD3D are unavailable with the runtime-supplied reason.
  const wine = rows.get('unavailable0');
  assert.equal(wine.state, STATE.UNAVAILABLE);
  assert.equal(wine.value, 'unavailable');
  assert.match(wine.note, /no host boundary/);

  // Frames with no present must render "--", never 0.
  const idle = createMetricsModel();
  idle.setRunState('running');
  idle.applyWasm({ ...WASM_FULL, present: { vk: 0, vk32: 0, sdl: 0, count: 0, fps: null, frameMs: null, path: 'none' }, vk: { ...WASM_FULL.vk, frames: 0 } }, at(0));
  const idleRows = new Map();
  for (const group of idle.view(at(100)).groups) for (const row of group.rows) idleRows.set(row.id, row);
  assert.equal(idleRows.get('fps').value, '--');
  assert.equal(idleRows.get('frameMs').value, '--');
  assert.equal(idleRows.get('fps').state, STATE.INACTIVE);
});

test('stale snapshots keep the last real values instead of resetting to zero', () => {
  const model = createMetricsModel({ staleAfterMs: 3000 });
  model.setRunState('running');
  model.applyWasm(WASM_FULL, at(1000));
  assert.equal(model.view(at(2000)).stale, false);
  const stale = model.view(at(9000));
  assert.equal(stale.stale, true, '4s without a snapshot while running is stale');
  const rows = new Map();
  for (const group of stale.groups) for (const row of group.rows) rows.set(row.id, row);
  assert.equal(rows.get('vkSubmit').value, '9.30 ms', 'stale rows keep their last measured value');
  assert.equal(rows.get('vkSubmit').state, STATE.STALE);
  assert.equal(stale.wasmAgeMs, 8000);
  // Idle (not running) is never stale: the panel just stops updating.
  model.setRunState('stopped');
  assert.equal(model.view(at(9000)).stale, false);
});

// ---------------------------------------------------------------------------
// Honesty rules
// ---------------------------------------------------------------------------
test('overlapping stages are labelled as overlapping and never summed', () => {
  const model = createMetricsModel();
  model.setRunState('running');
  model.applyWasm(WASM_FULL, at(0));
  model.applyPage(PAGE_FULL, at(0));
  const view = model.view(at(100));
  const rows = new Map();
  for (const group of view.groups) for (const row of group.rows) rows.set(row.id, row);
  assert.match(rows.get('vkSubmit').note, /Overlaps/);
  assert.match(rows.get('uploads').note, /subsets of the submit row, never additive/);
  assert.match(rows.get('guestSpan').note, /NOT OS process CPU/);
  assert.match(rows.get('submitJs').note, /NOT GPU execution time/);
  assert.match(rows.get('spirv').note, /per translation EVENT/);
  // Uploads are shown as buffer+image only, and that sum is stated as inside submit.
  assert.equal(rows.get('uploads').value, '7.10 ms');
  assert.match(rows.get('uploads').unit, /inside submit/);
  const total = view.groups.flatMap(g => g.rows).find(r => r.id === 'total');
  assert.equal(total, undefined, 'there is no fabricated total row');
});

test('guest layers link to the host-visible footprint rather than a guessed duration', () => {
  const model = createMetricsModel();
  model.setRunState('running');
  model.applyWasm(WASM_FULL, at(0));
  const view = model.view(at(10));
  const sql = view.groups.flatMap(g => g.rows).find(r => r.id === 'unavailable1');
  assert.match(sql.note, /only its Vulkan calls are host-visible/);
  assert.equal(view.groups.some(g => g.rows.some(r => r.value && /ms/.test(r.value) && r.id.startsWith('unavailable'))), false,
    'no unavailable row may carry a duration');
});

test('JIT rows separate absent, present-but-disabled and running engines', () => {
  const model = createMetricsModel();
  model.setRunState('running');
  model.applyWasm(WASM_FULL, at(0));
  const rows = new Map();
  for (const group of model.view(at(10)).groups) for (const row of group.rows) rows.set(row.id, row);
  assert.equal(rows.get('jit-cpu32').value, 'unavailable');
  assert.match(rows.get('jit-cpu32').note, /no x86 guest JIT in this runtime build/);
  assert.equal(rows.get('jit-cpu64').value, 'running \u00b7 emitted-wasm');
  assert.equal(rows.get('jit-cpu64').state, STATE.MEASURED);
  assert.equal(rows.get('jit-cpu64').unit, '81.23M instructions retired');
  assert.match(rows.get('jit-cpu64').note, /33.2k block entries/);

  // Compiled in but disabled: inactive, and never conflated with "absent".
  const disabled = JSON.parse(JSON.stringify(WASM_FULL));
  disabled.jit.cpu64.enabled = false;
  const m2 = createMetricsModel();
  m2.setRunState('running');
  m2.applyWasm(disabled, at(0));
  const rows2 = new Map();
  for (const group of m2.view(at(10)).groups) for (const row of group.rows) rows2.set(row.id, row);
  assert.equal(rows2.get('jit-cpu64').value, 'disabled \u00b7 emitted-wasm');
  assert.equal(rows2.get('jit-cpu64').state, STATE.INACTIVE);

  // available=false means the engine did not publish: not "0 blocks emitted".
  const notInstrumented = JSON.parse(JSON.stringify(WASM_FULL));
  notInstrumented.jit.cpu64.available = false;
  const m3 = createMetricsModel();
  m3.setRunState('running');
  m3.applyWasm(notInstrumented, at(0));
  const rows3 = new Map();
  for (const group of m3.view(at(10)).groups) for (const row of group.rows) rows3.set(row.id, row);
  assert.equal(rows3.get('jit-cpu64').value, 'not instrumented');
  assert.equal(rows3.get('jit-cpu64').state, STATE.UNAVAILABLE);
});

test('GPU execution falls back to unavailable with the feature name, not to submit time', () => {
  const model = createMetricsModel();
  model.setRunState('running');
  model.applyWasm(WASM_FULL, at(0));
  model.applyPage({
    ...PAGE_FULL,
    v2: { ...PAGE_FULL.v2, gpu: { status: 'unavailable', reason: 'WebGPU adapter does not expose the timestamp-query feature', ms: null, lastMs: null, samples: 0 } },
  }, at(0));
  const rows = new Map();
  for (const group of model.view(at(10)).groups) for (const row of group.rows) rows.set(row.id, row);
  assert.equal(rows.get('gpuExec').value, '--');
  assert.equal(rows.get('gpuExec').state, STATE.UNAVAILABLE);
  assert.match(rows.get('gpuExec').note, /timestamp-query/);
  assert.equal(rows.get('submitJs').value, '12.5 ms', 'the JS submit span stays visible under its own label');
});

test('no page-tier renderer yet reports GPU execution unavailable, not inactive', () => {
  const model = createMetricsModel();
  model.setRunState('running');
  model.applyWasm(WASM_FULL, at(0));          // guest running, no page-tier snapshot
  const rows = new Map();
  for (const group of model.view(at(10)).groups) for (const row of group.rows) rows.set(row.id, row);
  assert.equal(rows.get('gpuExec').value, '--');
  assert.equal(rows.get('gpuExec').state, STATE.UNAVAILABLE);
  assert.match(rows.get('gpuExec').note, /no page-tier snapshot yet/);
  assert.equal(rows.get('submitJs').state, STATE.UNAVAILABLE, 'JS submit span is unavailable without a page tier');
});

test('a runtime with no metrics snapshot reports runtime stages unavailable', () => {
  const model = createMetricsModel();
  model.setRunState('running');
  model.applyPage(PAGE_FULL, at(0));
  const view = model.view(at(10));
  const rows = new Map();
  for (const group of view.groups) for (const row of group.rows) rows.set(row.id, row);
  assert.equal(rows.get('fps').value, '60.0', 'page-tier rows still work without the runtime half');
  assert.equal(rows.get('unavailable-runtime').state, STATE.UNAVAILABLE);
  assert.match(rows.get('unavailable-runtime').note, /bw64: METRICS/);
  assert.equal(view.arch, null);
});

test('32-bit guest Vulkan reports its wall span and refuses a breakdown', () => {
  const model = createMetricsModel();
  model.setRunState('running');
  const vk32 = JSON.parse(JSON.stringify(WASM_FULL));
  vk32.present = { vk: 0, vk32: 40, sdl: 0, count: 40, fps: 40.2, frameMs: 24.9, path: 'vk32' };
  vk32.vk32 = { supported: true, frames: 40, wallMs: 401.0, breakdown: false, reason: '32-bit guest Vulkan presents have no per-trap timers; submit/trap/hop and the guest span are unavailable' };
  model.applyWasm(vk32, at(0));
  const view = model.view(at(10));
  const rows = new Map();
  for (const group of view.groups) for (const row of group.rows) rows.set(row.id, row);
  assert.equal(view.headline.path, 'vk32');
  assert.equal(rows.get('vulkan32').value, '401 ms');
  assert.equal(rows.get('vulkan32').state, STATE.MEASURED);
  assert.match(rows.get('vulkan32').note, /no per-trap timers/);
  assert.equal(rows.get('vulkan32').unit, '40 presents');
});

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------
test('formatters never turn a missing measurement into a number', () => {
  assert.equal(formatMs(null), '--');
  assert.equal(formatMs(undefined), '--');
  assert.equal(formatMs(NaN), '--');
  assert.equal(formatMs(0), '0 ms');
  assert.equal(formatMs(0.25), '0.25 ms');
  assert.equal(formatMs(4.239), '4.24 ms');
  assert.equal(formatMs(16.83), '16.8 ms');
  assert.equal(formatMs(1234.2), '1,234 ms');
  assert.equal(formatCount(null), '--');
  assert.equal(formatCount(33210), '33.2k');
  assert.equal(formatCount(81234567), '81.23M');
  assert.equal(formatFps(null), '--');
  assert.equal(formatFps(59.97), '60.0');
  assert.equal(formatBytes(null), '--');
  assert.equal(formatBytes(65536), '64.0 kB');
  assert.equal(formatAge(null), '--');
  assert.equal(formatAge(900), '900 ms');
  assert.equal(formatAge(9000), '9.0 s');
});

// ---------------------------------------------------------------------------
// Panel behaviour
// ---------------------------------------------------------------------------
test('panel shows only FPS, frame time, CPU time and GPU time', () => {
  const doc = makeDom();
  const panel = createMetricsPanel({ doc });
  const model = createMetricsModel();
  model.setRunState('running');
  model.applyWasm(WASM_FULL, at(0));
  model.applyPage(PAGE_FULL, at(0));

  panel.setOpen(true);
  assert.equal(panel.hidden, false);
  panel.update(model.view(at(10)));
  const first = rowsByLabel(panel.element);
  assert.deepEqual([...first.keys()], ['FPS', 'Frame time', 'CPU time', 'GPU time']);
  assert.equal(first.get('GPU time').value, '4.25 ms');
  assert.equal(first.get('CPU time').value, '483 ms');
  assert.equal(first.get('FPS').state, 'measured');

  // Refresh 50 times: the four rows are reused, not rebuilt, and the last value wins.
  for (let i = 0; i < 50; i++) {
    model.applyWasm({ ...WASM_FULL, vk: { ...WASM_FULL.vk, guestMs: 5 + i * 0.01 } }, at(20 + i));
    panel.update(model.view(at(30 + i)));
  }
  const panelRows = walk(panel.element).filter(n => n.className === 'metrics-row');
  assert.equal(panelRows.length, 4, 'no row duplication across 50 refreshes');
  assert.equal(rowsByLabel(panel.element).get('CPU time').value, '5.49 ms');
});

test('panel shows -- for numbers the runtime cannot measure, never 0', () => {
  const doc = makeDom();
  const panel = createMetricsPanel({ doc });
  const model = createMetricsModel();
  model.setRunState('running');
  panel.setOpen(true);
  panel.update(model.view(at(0)));
  const rows = rowsByLabel(panel.element);
  for (const label of ['FPS', 'Frame time', 'CPU time', 'GPU time']) {
    assert.equal(rows.get(label).value, '--', label);
    assert.equal(rows.get(label).state, 'unavailable', label);
  }
});

test('panel greys the last numbers when the runtime stops reporting', () => {
  const doc = makeDom();
  const panel = createMetricsPanel({ doc });
  const model = createMetricsModel({ staleAfterMs: 3000 });
  model.setRunState('running');
  model.applyWasm(WASM_FULL, at(0));
  panel.setOpen(true);
  panel.update(model.view(at(100)));
  assert.equal(panel.element.dataset.stale, '0');
  panel.update(model.view(at(9000)));
  assert.equal(panel.element.dataset.stale, '1');
  assert.match(textOf(panel.element), /paused/);
  assert.equal(rowsByLabel(panel.element).get('FPS').state, 'stale');
  assert.equal(rowsByLabel(panel.element).get('FPS').value, '59.6', 'stale panel still shows the last measured value');
});

test('installFrameMetrics feeds runtime lines, polls the tier and closes cheaply', async () => {
  const doc = makeDom();
  let tier = null;
  const host = installFrameMetrics({ doc, tierProvider: () => tier, intervalMs: 250 });
  assert.equal(host.feed('wine: booting'), false);
  assert.equal(host.feed('bw64: METRICS ' + JSON.stringify(WASM_FULL)), true);
  assert.equal(host.model.hasWasm, true);

  tier = { metrics: () => PAGE_FULL };
  host.setRunState('running');
  host.start();
  // The 250 ms poll is exercised directly instead of waiting on a real timer.
  host.panel.update(host.model.view());
  assert.equal(host.model.hasPage, true);
  const rows = rowsByLabel(host.element);
  assert.equal(rows.get('FPS').value, '60.0');

  // Closing stops rendering but keeps the aggregation model usable.
  host.setOpen(false);
  assert.equal(host.panel.hidden, true);
  assert.equal(host.feed('bw64: METRICS ' + JSON.stringify({ ...WASM_FULL, arch: 'x86' })), true);
  assert.equal(host.model.view().arch, 'x86');
  host.destroy();
  assert.equal(host.element.parent, null);
});

// ---------------------------------------------------------------------------
// Source-level guards: the panel must not reintroduce the things it forbids
// ---------------------------------------------------------------------------
test('page tier reports GPU time only from timestamp queries', () => {
  const page = readFileSync(join(repoRoot, 'web/vkwebgpu.mjs'), 'utf8');
  assert.ok(page.includes("has('timestamp-query')") || page.includes("'timestamp-query'"),
    'the timestamp-query feature must be required for GPU timing');
  assert.ok(page.includes('writeTimestamp'), 'GPU timing must use writeTimestamp');
  assert.ok(page.includes('createGpuTimer'), 'the GPU timer must exist');
  assert.ok(/queue\.submit time is host-side call cost and is never reported as GPU time/.test(page),
    'the submit-vs-GPU distinction must stay documented at the source');
});

test('the metrics registry never emits a fabricated duration for guest layers', () => {
  const registry = readFileSync(join(repoRoot, 'source/util/bw64metrics.cpp'), 'utf8');
  assert.ok(registry.includes('"id\\":\\"wine\\"'), 'wine is declared unavailable in the JSON');
  assert.ok(registry.includes('"id\\":\\"dxvk\\"'), 'dxvk is declared unavailable in the JSON');
  assert.ok(registry.includes('breakdown\\":false'), 'the 32-bit path refuses to report a breakdown');
  assert.ok(registry.includes('bw64: METRICS '), 'the emitted line carries the documented prefix');
  assert.ok(!/\bguestMs\s*=\s*wall\s*\+\s*/.test(registry), 'the guest span is derived by subtraction only');
});