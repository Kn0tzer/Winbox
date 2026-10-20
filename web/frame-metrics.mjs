// web/frame-metrics.mjs — live Winbox frame-stage metrics: aggregation + panel.
//
// Two inputs are merged, never conflated:
//
//   1. The wasm runtime's `bw64: METRICS {...}` line (source/util/bw64metrics.cpp,
//      parsed by parseMetricsLine). It carries the Vulkan frame boundary
//      (present-to-present wall, submit, other traps, present body, worker->page
//      hop), the guest-visible stage spans (syscalls / GL bridge / audio bridge),
//      and the JIT counters published by each architecture.
//   2. The page tier's own snapshot (window.bwVkTier.metrics()): displayed-frame
//      pacing, SPIR-V -> WGSL translation, pipeline creation, JS submit, and the
//      ONLY genuine GPU execution number in this file, from WebGPU timestamp
//      queries.
//
// Rules this module exists to enforce (docs/reports/winbox-metrics-contract-20261018.md):
//   * a stage is `measured`, `inactive` (instrumented, no events in the window),
//     `unavailable` (the build/backend cannot report it, with a reason) or
//     `stale` (no fresh wasm snapshot while the program is running);
//   * `null` is shown as "--", never as 0;
//   * inclusive spans (submit, present, hop, trapOther, buffer/image uploads) all
//     overlap inside the frame window; nothing is summed into a fake total;
//   * the guest CPU span is a WALL span, not OS process CPU accounting;
//   * JS queue.submit time is labelled "JS" and is never presented as GPU time;
//   * Wine, DXVK and VKD3D run as guest code and are reported unavailable, with
//     their host-visible footprint linked to the Vulkan rows rather than guessed.
export const METRICS_LINE_PREFIX = 'bw64: METRICS ';
export const STALE_AFTER_MS = 3000;

export const STATE = {
  MEASURED: 'measured',
  INACTIVE: 'inactive',
  UNAVAILABLE: 'unavailable',
  STALE: 'stale',
};

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------
/**
 * Parse one runtime log line. Returns the snapshot object, or null when the line
 * is not a metrics snapshot (or is damaged — a damaged line must never throw).
 * The runtime's stdout hop may join several lines into one string, so the JSON
 * ends at the first newline after the prefix.
 * @param {string} line
 */
export function parseMetricsLine(line) {
  if (typeof line !== 'string') return null;
  const at = line.indexOf(METRICS_LINE_PREFIX);
  if (at < 0) return null;
  let json = line.slice(at + METRICS_LINE_PREFIX.length);
  const newline = json.indexOf('\n');
  if (newline >= 0) json = json.slice(0, newline);
  json = json.trim();
  if (!json.startsWith('{') || !json.endsWith('}')) return null;
  try {
    const parsed = JSON.parse(json);
    return parsed && typeof parsed === 'object' && parsed.v === 1 ? parsed : null;
  } catch {
    return null;
  }
}

// ---------------------------------------------------------------------------
// Formatting helpers (shared by the panel and the tests)
// ---------------------------------------------------------------------------
export function formatMs(value) {
  if (!isFinite(value) || value === null || value === undefined) return '--';
  if (value === 0) return '0 ms';
  // Two decimals below 10 ms: submit/present/GPU spans are routinely sub-ms and
  // rounding them to one decimal would hide the difference between stages.
  if (value < 10) return value.toFixed(2) + ' ms';
  if (value < 100) return value.toFixed(1) + ' ms';
  return Math.round(value).toLocaleString() + ' ms';
}

/** Age of the last snapshot, for the stale banner (ms under a second, then s). */
export function formatAge(value) {
  if (!isFinite(value) || value === null || value === undefined) return '--';
  if (value < 1000) return Math.round(value) + ' ms';
  return (value / 1000).toFixed(1) + ' s';
}

export function formatCount(value) {
  if (value === null || value === undefined || !isFinite(value)) return '--';
  if (value >= 1e9) return (value / 1e9).toFixed(2) + 'B';
  if (value >= 1e6) return (value / 1e6).toFixed(2) + 'M';
  if (value >= 1e4) return (value / 1e3).toFixed(1) + 'k';
  return Math.round(value).toLocaleString();
}

export function formatFps(value) {
  if (!isFinite(value) || value === null || value === undefined) return '--';
  return value.toFixed(1);
}

export function formatBytes(value) {
  if (value === null || value === undefined || !isFinite(value)) return '--';
  if (value >= 1024 ** 2) return (value / 1024 ** 2).toFixed(1) + ' MB';
  if (value >= 1024) return (value / 1024).toFixed(1) + ' kB';
  return Math.round(value) + ' B';
}

// ---------------------------------------------------------------------------
// Model: merge the two sources and classify every stage
// ---------------------------------------------------------------------------
/**
 * @param {{staleAfterMs?: number}} [options] how long a `stale` verdict counts as fresh
 */
export function createMetricsModel({ staleAfterMs = STALE_AFTER_MS } = {}) {
  let wasm = null, wasmAt = 0, page = null, pageAt = 0, runState = 'idle';
  let lastStale = false;

  return {
    /** @param {any} snapshot parsed `bw64: METRICS` object */
    applyWasm(snapshot, now = Date.now()) {
      if (!snapshot) return;
      wasm = snapshot;
      wasmAt = now;
    },
    /** @param {any} snapshot window.bwVkTier.metrics() */
    applyPage(snapshot, now = Date.now()) {
      if (!snapshot) return;
      page = snapshot;
      pageAt = now;
    },
    /** 'idle' | 'running' | 'stopped' — drives the stale verdict only. */
    setRunState(state) { runState = state; },
    reset() { wasm = null; wasmAt = 0; page = null; pageAt = 0; lastStale = false; },
    get hasWasm() { return !!wasm; },
    get hasPage() { return !!page; },
    /** Wall-clock age of the last wasm snapshot (ms), or null when none. */
    wasmAgeMs(now = Date.now()) { return wasm ? now - wasmAt : null; },

    /**
     * Build the renderable view. Called on the panel's tick (1 Hz by default).
     */
    view(now = Date.now()) {
      const ageMs = wasm ? now - wasmAt : null;
      const pageAgeMs = page ? now - pageAt : null;
      // "Stale" only means: the program is running and the wasm snapshot stopped
      // arriving. Values are still the last real ones, so they stay visible,
      // marked as stale rather than being replaced by zeros.
      const stale = runState === 'running' && ageMs !== null && ageMs > staleAfterMs;
      lastStale = stale;
      const wasmState = (active) => {
        if (!wasm) return STATE.UNAVAILABLE;
        if (stale) return STATE.STALE;
        return active ? STATE.MEASURED : STATE.INACTIVE;
      };

      const present = wasm?.present || null;
      const vk = wasm?.vk || null;
      const vk32 = wasm?.vk32 || null;
      const stages = wasm?.stages || null;
      const jit = wasm?.jit || null;

      // Displayed-frame pacing prefers the page tier (it counts frames that
      // reached the canvas); the wasm side counts guest presents. Both are
      // shown, labelled, side by side — they answer different questions.
      const pagePace = page?.v2 || page?.v1 || null;
      const headline = {
        fps: pagePace?.fps ?? present?.fps ?? null,
        fpsSource: pagePace?.fps != null ? 'page (displayed)' : (present?.fps != null ? 'guest presents' : null),
        frameMs: pagePace?.frameMs ?? present?.frameMs ?? null,
        presents: pagePace?.presents ?? null,
        path: present?.path || (page?.v2 ? 'vk2' : null),
      };

      const groups = [];
      groups.push({
        id: 'frames', title: 'Frames',
        rows: [
          {
            id: 'fps', label: 'FPS', value: formatFps(headline.fps),
            unit: 'displayed frames/s',
            state: headline.fps != null ? wasmState(true) : STATE.INACTIVE,
            note: headline.fps === null ? 'no presented frame in the current window'
              : 'measured over the last presents on this page' + (headline.fpsSource ? ' (' + headline.fpsSource + ')' : ''),
          },
          {
            id: 'frameMs', label: 'Frame time', value: formatMs(headline.frameMs),
            unit: 'per displayed frame',
            state: headline.frameMs != null ? wasmState(true) : STATE.INACTIVE,
            note: 'mean interval between presented frames',
          },
          {
            id: 'presentCount', label: 'Presented frames', value: formatCount(present?.count ?? null),
            unit: 'in the last ' + formatMs(wasm?.winMs ?? null) + ' window',
            state: wasmState(!!present?.count),
            note: present ? ('path ' + present.path + ' — vulkan ' + formatCount(present.vk) + ', vulkan32 ' + formatCount(present.vk32) + ', sdl ' + formatCount(present.sdl)) : 'no runtime snapshot yet',
          },
        ],
      });

      groups.push({
        id: 'cpu', title: 'Guest CPU and host bridge',
        rows: [
          {
            id: 'guestSpan', label: 'Guest CPU active span', value: formatMs(vk?.guestMs ?? null),
            unit: 'wall span / frame',
            state: wasmState(!!vk?.frames),
            note: 'present-to-present wall minus submit, other traps and the present body. A wall span, NOT OS process CPU accounting.',
          },
          {
            id: 'vkSubmit', label: 'Vulkan submit', value: formatMs(vk?.submitMs ?? null),
            unit: 'inclusive / frame',
            state: wasmState(!!vk?.frames),
            note: 'vkQueueSubmit handler: command walk + uploads + page hop. Overlaps the buffer/image rows below.',
          },
          {
            id: 'vkTraps', label: 'Other Vulkan traps', value: formatMs(vk?.trapOtherMs ?? null),
            unit: formatCount(vk?.trapCalls ?? null) + ' calls',
            state: wasmState(!!vk?.trapCalls),
            note: 'recording traps (vkCmd*, acquire, create) that are not submit or present',
          },
          {
            id: 'vkPresent', label: 'Vulkan present body', value: formatMs(vk?.presentMs ?? null),
            unit: 'inclusive / frame',
            state: wasmState(!!vk?.frames),
            note: 'vkQueuePresentKHR handler, including the worker->page hop',
          },
          {
            id: 'hop', label: 'Worker to page hop', value: formatMs(vk?.hopMs ?? null),
            unit: formatCount(vk?.hopChunks ?? null) + ' chunks / ' + formatBytes(vk?.hopBytes ?? null),
            state: wasmState(!!vk?.hopChunks),
            note: 'chunk transfer into the page tier (inside present or submit)',
          },
          {
            id: 'syscall', label: 'Syscalls / host bridge', value: formatMs(stages?.syscall?.ms ?? null),
            unit: formatCount(stages?.syscall?.calls ?? null) + ' calls',
            state: stages?.syscall?.supported === false ? STATE.UNAVAILABLE : wasmState(!!stages?.syscall?.calls),
            note: stages?.syscall?.supported === false
              ? 'not compiled in this runtime build'
              : 'measured inside ' + (stages?.syscall?.calls ? 'guest ' + formatMs(vk?.guestMs ?? null) : 'the guest span') + '; GL and Vulkan traps excluded',
          },
        ],
      });

      groups.push({ id: 'jit', title: 'Guest CPU engines (JIT)', rows: describeJit(jit, stale) });

      groups.push({
        id: 'gpu', title: 'GPU and shading (page tier)',
        rows: [
          {
            id: 'gpuExec', label: 'GPU execution', value: formatMs(pagePace?.gpu?.status === STATE.MEASURED ? pagePace.gpu.ms : null),
            unit: pagePace?.gpu?.samples ? pagePace.gpu.samples + ' samples' : 'WebGPU timestamps',
            state: pagePace?.gpu?.status === STATE.MEASURED ? STATE.MEASURED
              : pagePace?.gpu?.status === STATE.INACTIVE ? STATE.INACTIVE : STATE.UNAVAILABLE,
            note: pagePace?.gpu?.status === STATE.MEASURED
              ? 'timestamp-query around this frame\'s submitted GPU work (real GPU time)'
              : (pagePace?.gpu?.reason
                || 'no page-tier snapshot yet \u2014 this program has not presented a WebGPU frame'),
          },
          {
            id: 'submitJs', label: 'Submit (JS, not GPU)', value: formatMs(pagePace?.submit?.ms ?? null),
            unit: formatCount(pagePace?.submit?.count ?? null) + ' submits',
            state: pagePace ? (pagePace.submit?.count ? STATE.MEASURED : STATE.INACTIVE) : STATE.UNAVAILABLE,
            note: 'JavaScript queue.submit call cost. This is NOT GPU execution time.',
          },
          {
            id: 'renderJs', label: 'Page render total (JS)', value: formatMs(pagePace?.render?.ms ?? null),
            unit: formatCount(pagePace?.render?.count ?? null) + ' frames',
            state: pagePace ? (pagePace.render?.count ? STATE.MEASURED : STATE.INACTIVE) : STATE.UNAVAILABLE,
            note: 'whole page-tier frame replay incl. the submit above; overlaps every GPU row',
          },
          {
            id: 'spirv', label: 'SPIR-V to WGSL', value: formatMs(pagePace?.translate?.ms ?? null),
            unit: formatCount(pagePace?.translate?.count ?? null) + ' events',
            state: pagePace ? (pagePace.translate?.count ? STATE.MEASURED : STATE.INACTIVE) : STATE.UNAVAILABLE,
            note: 'milliseconds per translation EVENT (modules are cached, so this is not per frame)',
          },
          {
            id: 'pipeline', label: 'Pipeline creation', value: formatMs(pagePace?.pipeline?.ms ?? null),
            unit: formatCount(pagePace?.pipeline?.count ?? null) + ' pipelines',
            state: pagePace ? (pagePace.pipeline?.count ? STATE.MEASURED : STATE.INACTIVE) : STATE.UNAVAILABLE,
            note: 'createRenderPipeline + WGSL scan, per new pipeline state',
          },
          {
            id: 'uploads', label: 'Buffer / image uploads', value: formatMs((vk?.bufMs ?? 0) + (vk?.imgMs ?? 0)),
            unit: 'inside submit',
            state: wasmState(!!vk?.frames),
            note: 'buffer ' + formatMs(vk?.bufMs ?? null) + ' + image ' + formatMs(vk?.imgMs ?? null) + '; subsets of the submit row, never additive to it',
          },
          {
            id: 'vulkan32', label: '32-bit guest Vulkan', value: formatMs(vk32?.frames ? vk32?.wallMs ?? null : null),
            unit: formatCount(vk32?.frames ?? null) + ' presents',
            state: vk32?.supported === false ? STATE.UNAVAILABLE
              : (vk32?.frames ? wasmState(true) : STATE.INACTIVE),
            note: vk32?.frames ? (vk32?.reason || 'present-to-present wall only')
              : 'no 32-bit guest presented through the Vulkan bridge in this window',
          },
          {
            id: 'rejects', label: 'Skipped / unsupported frames', value: formatCount(page ? (pagePace?.skipped ?? 0) + (pagePace?.unsupported ?? 0) : null),
            unit: 'page tier',
            state: page ? STATE.MEASURED : STATE.UNAVAILABLE,
            note: 'frames the page tier could not model (untranslatable SPIR-V, unknown format, rejected pipeline)',
          },
        ],
      });

      groups.push({
        id: 'audio', title: 'Audio and GL bridge',
        rows: [
          {
            id: 'audio', label: 'Audio bridge', value: formatMs(stages?.audio?.ms ?? null),
            unit: formatCount(stages?.audio?.calls ?? null) + ' writes / ' + formatBytes(stages?.audio?.bytes ?? null),
            state: stages?.audio?.supported === false ? STATE.UNAVAILABLE : wasmState(!!stages?.audio?.calls),
            note: 'guest write path: format conversion + push into the Web Audio ring. Not an audible-latency measure.',
          },
          {
            id: 'gl', label: 'GL bridge', value: formatMs(stages?.gl?.ms ?? null),
            unit: formatCount(stages?.gl?.calls ?? null) + ' traps',
            state: stages?.gl?.supported === false ? STATE.UNAVAILABLE : wasmState(!!stages?.gl?.calls),
            note: stages?.gl?.calls
              ? 'per guest->host GL trap, including marshaling and the WebGL2 calls it makes'
              : 'no GL traps in this window (this program does not use the OpenGL path)',
          },
        ],
      });

      // Guest-side layers the host cannot time. Their reasons come from the
      // runtime snapshot so this module never invents an explanation.
      const unavailableRows = (wasm?.unavailable || []).map((entry, index) => ({
        id: 'unavailable' + index,
        label: String(entry?.id || 'unknown'),
        value: 'unavailable',
        unit: '',
        state: STATE.UNAVAILABLE,
        note: String(entry?.reason || 'no reason reported'),
      }));
      if (page && pagePace?.gpu?.status === STATE.UNAVAILABLE) {
        unavailableRows.push({
          id: 'unavailable-gpu', label: 'GPU timestamps', value: 'unavailable', unit: '',
          state: STATE.UNAVAILABLE, note: pagePace.gpu.reason,
        });
      }
      if (!wasm) {
        unavailableRows.push({
          id: 'unavailable-runtime', label: 'runtime stages', value: 'unavailable', unit: '',
          state: STATE.UNAVAILABLE,
          note: 'no `bw64: METRICS` snapshot received yet — the runtime may predate this instrumentation',
        });
      }
      groups.push({ id: 'layers', title: 'Guest layers (not host-visible)', rows: unavailableRows });

      return {
        version: wasm?.v ?? null,
        arch: wasm?.arch ?? null,
        uptimeMs: wasm?.uptimeMs ?? null,
        windowMs: wasm?.winMs ?? null,
        wasmAgeMs: ageMs,
        pageAgeMs,
        stale,
        enabled: wasm?.enabled !== false,
        headline,
        groups,
      };
    },
    get lastWasStale() { return lastStale; },
  };
}

// The JIT sections are the one place the panel must render an engine that is
// present-but-disabled differently from one that is absent from the build.
function describeJit(jit, stale) {
  const rows = [];
  const sections = [['cpu64', 'x86-64 guest'], ['cpu32', 'x86 guest']];
  for (const [key, label] of sections) {
    const s = jit?.[key];
    const id = 'jit-' + key;
    if (!s) {
      rows.push({
        id, label, value: 'unavailable', unit: '', state: STATE.UNAVAILABLE,
        note: jit ? 'the runtime reports no ' + key + ' JIT section' : 'no runtime snapshot yet',
      });
      continue;
    }
    if (!s.supported) {
      rows.push({
        id, label, value: 'unavailable', unit: '', state: STATE.UNAVAILABLE,
        note: 'no ' + label + ' JIT in this runtime build (interpreter only)',
      });
      continue;
    }
    if (!s.available) {
      rows.push({
        id, label, value: 'not instrumented', unit: '', state: STATE.UNAVAILABLE,
        note: 'the ' + label + ' engine exists but published no statistics (see the hook contract)',
      });
      continue;
    }
    const engine = s.engine || 'unknown engine';
    const value = (s.enabled ? 'running' : 'disabled') + ' \u00b7 ' + engine;
    rows.push({
      id, label, value, unit: formatCount(s.instructionsRetired) + ' instructions retired',
      state: !s.enabled ? STATE.INACTIVE : (stale ? STATE.STALE : STATE.MEASURED),
      note: formatCount(s.blocksEntered) + ' block entries \u00b7 ' + formatCount(s.blocksEmitted) + ' emitted \u00b7 '
        + formatCount(s.blocksInstantiated) + ' instantiated \u00b7 ' + formatCount(s.fallbacks) + ' fallbacks \u00b7 emit '
        + formatMs(s.emitUs / 1000) + ' \u00b7 instantiate ' + formatMs(s.instantiateUs / 1000),
    });
  }
  return rows;
}

// ---------------------------------------------------------------------------
// Panel: a compact overlay inside the player, built from the established UI
// vocabulary (same document, same CSS variables, no new design system).
// Updates are cheap by construction: row elements are created once and only
// changed text nodes are written.
// ---------------------------------------------------------------------------
/**
 * @param {{doc?: Document, onClose?: () => void}} [options]
 */
export function createMetricsPanel({ doc = globalThis.document, onClose = null } = {}) {
  const el = (tag, className, text) => {
    const node = doc.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  const root = el('section', 'metrics-panel');
  root.setAttribute('aria-label', 'Frame metrics');
  root.hidden = true;

  const head = el('div', 'metrics-head');
  const title = el('h2', 'metrics-title', 'Frame metrics');
  const status = el('span', 'metrics-status');
  const close = el('button', 'metrics-close', '\u2715');
  close.type = 'button';
  close.setAttribute('aria-label', 'Hide frame metrics');
  if (onClose) close.onclick = onClose;
  head.append(title, status, close);

  const scroll = el('div', 'metrics-scroll');
  const summaryRows = el('dl', 'metrics-rows metrics-summary');
  const more = el('details', 'metrics-more');
  const moreTitle = el('summary', '', 'More');
  const moreContent = el('div', 'metrics-more-content');
  const legend = el('p', 'metrics-legend',
    'measured / inactive / unavailable / stale \u00b7 a wall span, not OS CPU \u00b7 overlapping stages are never summed');
  const body = el('div', 'metrics-body');
  moreContent.append(legend, body);
  more.append(moreTitle, moreContent);
  scroll.append(summaryRows, more);
  root.append(head, scroll);

  /** @type {Map<string, {row: any, label: any, value: any, unit: any, note: any}>} */
  const rowCache = new Map();
  /** @type {Map<string, {section: any, title: any, rows: any}>} */
  const groupCache = new Map();
  const setData = (node, key, value) => { (/** @type {any} */ (node)).dataset[key] = value; };
  function setText(node, value) {
    if (node.textContent !== value) node.textContent = value;
  }

  function ensureGroup(group) {
    let cached = groupCache.get(group.id);
    if (!cached) {
      const section = el('section', 'metrics-group');
      const heading = el('h3', 'metrics-group-title', group.title);
      const rows = el('dl', 'metrics-rows');
      section.append(heading, rows);
      body.append(section);
      cached = { section, title: heading, rows };
      groupCache.set(group.id, cached);
    }
    return cached;
  }

  function ensureRow(group, row) {
    let cached = rowCache.get(row.id);
    if (!cached) {
      const line = el('div', 'metrics-row');
      const label = el('dt', 'metrics-label', row.label);
      const valueBox = el('dd', 'metrics-value');
      const value = el('span', 'metrics-number');
      const unit = el('span', 'metrics-unit');
      const note = el('p', 'metrics-note');
      valueBox.append(value, unit);
      line.append(label, valueBox, note);
      cached = { row: line, label, value, unit, note };
      rowCache.set(row.id, cached);
    }
    if (cached.label.textContent !== row.label) cached.label.textContent = row.label;
    return cached;
  }

  return {
    element: root,
    get hidden() { return root.hidden; },
    setOpen(open) { root.hidden = !open; },
    /** One cheap refresh; called at ~1 Hz by the app shell. */
    update(view) {
      setText(status, view.stale
        ? 'stale ' + formatAge(view.wasmAgeMs)
        : (view.enabled ? 'live' : 'disabled'));
      setData(status, 'state', view.stale ? STATE.STALE : (view.enabled ? STATE.MEASURED : STATE.UNAVAILABLE));
      setData(root, 'stale', view.stale ? '1' : '0');

      const seen = new Set();
      const seenGroups = new Set();
      for (const group of view.groups || []) {
        const summary = group.id === 'frames' || group.id === 'jit'
          ? group.rows || []
          : group.id === 'gpu' ? (group.rows || []).filter(row => row.id === 'gpuExec') : [];
        const summaryIds = new Set(summary.map(row => row.id));
        const detailRows = (group.rows || []).filter(row => !summaryIds.has(row.id));
        for (const row of group.rows || []) {
          const entry = ensureRow(group, row);
          seen.add(row.id);
          const value = row.value === null || row.value === undefined ? '--' : String(row.value);
          if (entry.value.textContent !== value) entry.value.textContent = value;
          let unit = row.unit ? String(row.unit) : '';
          let noteText = String(row.note || '');
          if (summaryIds.has(row.id)) {
            if (group.id === 'jit') {
              const blockEntries = noteText.match(/^([^\u00b7]*?\bblock entries)(?:\s*\u00b7|$)/);
              if (blockEntries) unit = blockEntries[1].trim();
            }
            noteText = '';
          }
          if (entry.unit.textContent !== unit) entry.unit.textContent = unit;
          if (entry.note.textContent !== noteText) entry.note.textContent = noteText;
          if ((/** @type {any} */ (entry.row)).dataset.state !== row.state) setData(entry.row, 'state', row.state);
          if (summaryIds.has(row.id)) summaryRows.append(entry.row);
          else if (detailRows.length) ensureGroup(group).rows.append(entry.row);
        }
        if (detailRows.length) {
          const cached = ensureGroup(group);
          seenGroups.add(group.id);
          for (const row of detailRows) cached.rows.append(rowCache.get(row.id).row);
        }
      }
      for (const [id, entry] of rowCache) if (!seen.has(id)) { entry.row.remove(); rowCache.delete(id); }
      for (const [id, cached] of groupCache) if (!seenGroups.has(id)) { cached.section.remove(); groupCache.delete(id); }
    },
    destroy() { root.remove(); rowCache.clear(); groupCache.clear(); },
  };
}

// ---------------------------------------------------------------------------
// App-shell binding: one object that owns the model, the panel, the poll timer
// and the wasm log-line feed.
// ---------------------------------------------------------------------------
/**
 * @param {{doc?: Document, tierProvider?: () => any, intervalMs?: number,
 *          onVisibilityChange?: (open: boolean) => void, open?: boolean}} [options]
 */
export function installFrameMetrics({
  doc = globalThis.document, tierProvider = () => null, intervalMs = 1000,
  onVisibilityChange = null, open = true,
} = {}) {
  const model = createMetricsModel();
  const panel = createMetricsPanel({
    doc,
    onClose: () => { setOpen(false); if (onVisibilityChange) onVisibilityChange(false); },
  });
  let timer = null, destroyed = false, isOpen = open;
  const refresh = () => {
    if (destroyed) return;
    let tier = null;
    try { tier = tierProvider(); } catch { tier = null; }
    let snapshot = null;
    try { snapshot = tier && typeof tier.metrics === 'function' ? tier.metrics() : null; } catch { snapshot = null; }
    if (snapshot) model.applyPage(snapshot);
    if (!isOpen) return;          // closed panel: aggregate nothing, render nothing
    panel.update(model.view());
  };
  const startTimer = () => {
    if (timer || destroyed) return;
    timer = setInterval(refresh, Math.max(250, intervalMs));
    refresh();
  };
  const stopTimer = () => {
    if (timer) clearInterval(timer);
    timer = null;
  };
  const setOpen = (value) => {
    isOpen = !!value;
    panel.setOpen(isOpen);
    if (isOpen) startTimer(); else stopTimer();
    refresh();
  };
  return {
    model,
    panel,
    element: panel.element,
    get open() { return isOpen; },
    setOpen,
    /** Feed a runtime log line; returns true when it was a metrics snapshot. */
    feed(line) {
      const snapshot = parseMetricsLine(line);
      if (!snapshot) return false;
      model.applyWasm(snapshot);
      if (isOpen) panel.update(model.view());
      return true;
    },
    start() { startTimer(); },
    stop() { stopTimer(); },
    setRunState(state) { model.setRunState(state); },
    reset() { model.reset(); },
    destroy() {
      destroyed = true;
      this.stop();
      panel.destroy();
    },
  };
}
