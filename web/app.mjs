import { fromFiles, fromDrop, fromZip, cleanupExtracted, ownFiles, sourceDirectory, retainExtracted, removeSource, peArchitecture, safePath, checkEntries, folderLimits, limits } from './files.mjs';
import { listPackages, getPackage, savePackage, deletePackage } from './storage.mjs';
import { detectProgram, runtimeParameters } from './launch.mjs';
import { demos, demoParameters } from './demos.mjs';
import { formatBytes } from './games.mjs';
import { prepareArchive, removeArchive } from './package-archive.mjs';
import { storedFiles, snapshotRuntime, readSaveBlobs } from './session-storage.mjs';
import { replaceInstallation } from './installation.mjs';
import { startupProgress, startupFailure, visibleFrame } from './startup.mjs';
import { readDirectory, openDirectPackage } from './direct-folder.mjs';
import { installFrameMetrics, METRICS_LINE_PREFIX } from './frame-metrics.mjs';
import { connectRuntimeClipboard } from './clipboard-bridge.mjs';

const $ = id => /** @type {any} */ (document.getElementById(id));
const icon = name => {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.classList.add('icon'); svg.setAttribute('aria-hidden', 'true');
  const use = document.createElementNS(svg.namespaceURI, 'use');
  use.setAttribute('href', 'assets/icons.svg#' + name); svg.append(use); return svg;
};
const text = (tag, value, className = '') => {
  const node = document.createElement(tag); node.textContent = value;
  if (className) node.className = className;
  return node;
};
let current = null, selected = null, busy = false, operation = null, lastAction = null;
let generation = 0, stopping = null;
// Live frame-stage metrics: one panel per running program, fed by the runtime's
// `bw64: METRICS` lines and the page tier's counters. Created in launchFrame,
// torn down in closeRuntime.
let metrics = null, metricsTier = null;
function metricsOpen() { return settings.metrics !== 'off'; }
function applyMetricsVisibility() {
  const open = metricsOpen() && !$('player').hidden;
  $('metricsHost').hidden = !open;
  $('stats').setAttribute('aria-pressed', String(open));
  $('stats').title = open ? 'Hide performance' : 'Show performance';
  $('stats').setAttribute('aria-label', $('stats').title);
  if (metrics) metrics.setOpen(open);
}
const settings = { appearance: 'dark', jit: 'off', internet: 'off', enhancement: 'off', folderMode: 'copy', metrics: 'off', shadercache: 'off' };
try { Object.assign(settings, JSON.parse(localStorage.getItem('wine.launcher.settings.v1') || '{}')); } catch {}
// Graphics backend and sharpness are no longer user settings; drop values saved by older versions.
delete settings.graphics; delete settings.sharpness;
settings.jit = settings.jit === 'on' ? 'on' : 'off';
settings.shadercache = settings.shadercache === 'on' ? 'on' : 'off';
function appearance(value) {
  document.documentElement.dataset.theme = ['light', 'dark'].includes(value) ? value : 'system';
  $('appearance').value = value;
  const dark = value === 'dark' || (value === 'system' && matchMedia('(prefers-color-scheme: dark)').matches);
  document.querySelector('meta[name="theme-color"]').setAttribute('content', dark ? '#000000' : '#ffffff');
}
appearance(settings.appearance);
for (const name of ['jit', 'internet', 'enhancement']) {
  $(name).value = settings[name];
  $(name).onchange = () => { settings[name] = $(name).value; persistSettings(); };
}
$('appearance').onchange = () => { settings.appearance = $('appearance').value; appearance(settings.appearance); persistSettings(); };
$('shadercache').value = settings.shadercache;
$('shadercache').onchange = () => { settings.shadercache = $('shadercache').value; persistSettings(); };
function persistSettings() { try { localStorage.setItem('wine.launcher.settings.v1', JSON.stringify(settings)); } catch {} }
function folderMode() {
  if (!window.showDirectoryPicker && settings.folderMode === 'direct') {
    settings.folderMode = 'copy';
    persistSettings();
  }
  $('folderMode').disabled = !window.showDirectoryPicker;
  $('folderMode').checked = settings.folderMode === 'direct';
}
folderMode();
$('folderMode').onchange = () => {
  settings.folderMode = $('folderMode').checked ? 'direct' : 'copy';
  persistSettings();
};
$('folderModeHelpButton').onclick = () => {
  const open = $('folderModeHelp').hidden;
  $('folderModeHelp').hidden = !open;
  $('folderModeHelpButton').setAttribute('aria-expanded', String(open));
};
function message(value, error = false) {
  $('notice').hidden = !value; $('noticeText').textContent = value; $('notice').classList.toggle('error', error);
}
function status(value) { $('status').textContent = value; }
function log(value) { $('log').textContent = ($('log').textContent + '\n' + value).slice(-48000); }
function showPlayerError(value) {
  $('loading').hidden = false;
  $('loading').classList.add('error-screen');
  $('loadingTitle').textContent = 'The program stopped';
  status(value);
  $('loadingActivity').hidden = true;
  $('launchProgress').hidden = true;
  $('loadingElapsed').hidden = true;
  $('loadingErrorActions').hidden = false;
  $('playerRetry').hidden = !lastAction;
}
function errorText(error) {
  if (error?.name === 'QuotaExceededError') return 'This browser is out of storage. Delete an unused folder or free some space, then try again.';
  return error?.message || String(error);
}
function lock(value) {
  busy = value;
  for (const button of document.querySelectorAll('[data-launch], #folderButton, #zipButton')) /** @type {HTMLButtonElement} */ (button).disabled = value || Boolean(stopping);
  $('restart').disabled = value || Boolean(stopping);
}
async function action(fn, { retry = fn } = {}) {
  if (busy || stopping) return;
  lock(true); operation = new AbortController(); const active = operation;
  if (retry) lastAction = retry;
  $('retry').hidden = true; $('playerRetry').hidden = true; $('loadingErrorActions').hidden = true;
  try { await fn(active.signal); }
  catch (error) {
    if (active.signal.aborted) return;
    const value = errorText(error); message(value, true);
    if (!$('player').hidden) showPlayerError(value);
    $('retry').hidden = !lastAction;
    if ($('folderDialog').open) { $('folderError').hidden = false; $('folderError').textContent = value; }
  } finally { if (operation === active) operation = null; lock(false); }
}
function preparing(title) {
  $('launcher').hidden = true; $('player').hidden = false;
  $('runtimeTitle').textContent = title;
  $('mouseCaptureHint').hidden = true;
  $('captureMouse').title = 'Capture mouse'; $('captureMouse').setAttribute('aria-label', 'Capture mouse'); $('captureMouse').setAttribute('aria-pressed', 'false');
  $('loading').classList.remove('error-screen');
  $('loading').classList.remove('waiting-for-frame');
  $('loadingTitle').textContent = 'Getting things ready'; $('loading').hidden = false;
  $('launchProgress').hidden = true; $('playerRetry').hidden = true;
  $('loadingErrorActions').hidden = true;
  $('loadingActivity').hidden = true; $('loadingElapsed').hidden = false; $('loadingElapsed').textContent = '';
  $('display').style.removeProperty('--program-aspect');
  $('log').textContent = ''; message('');
  $('runtimeTitle').setAttribute('tabindex', '-1'); $('runtimeTitle').focus({ preventScroll: true });
}
async function closeRuntime() {
  const run = current; current = null; generation++;
  if (metrics) { metrics.setRunState('stopped'); metrics.destroy(); metrics = null; }
  metricsTier = null;
  if (!run) return;
  clearInterval(run.progressTimer);
  let saveError;
  try { const saved = snapshotRuntime(run.frame); run.frame.remove(); await saved; } catch (error) { saveError = error; }
  finally {
    run.enhancer?.destroy(); run.frame.remove(); run.archive?.release(); run.renderer?.destroy(); run.canvas?.remove();
    const tier = await run.vkReady?.catch(() => null); await tier?.destroy();
    delete window.bwVkTier; delete window.bwVkTierReady; delete window.wineGpu;
  }
  if (saveError) throw Error('The program stopped, but its latest saves could not be stored. ' + errorText(saveError));
}
async function stop() {
  if (stopping) return stopping;
  operation?.abort();
  stopping = (async () => {
    let failure;
    try { await closeRuntime(); } catch (error) { failure = error; }
    $('player').hidden = true; $('launcher').hidden = false;
    $('launchProgress').hidden = true;
    $('metricsHost').hidden = true; $('stats').setAttribute('aria-pressed', 'false');
    message(failure ? errorText(failure) : '', Boolean(failure));
    $('folderButton').focus({ preventScroll: true });
  })();
  lock(busy);
  try { await stopping; } finally { stopping = null; lock(busy); }
}
async function launchFrame(query, title, archive = null) {
  query.set('jit', settings.jit === 'on' ? '1' : '0');
  query.set('shadercache', settings.shadercache === 'on' ? '1' : '0');
  if (settings.internet === 'on') { query.set('network', '1'); query.set('wisp', '1'); }
  if (settings.enhancement !== 'off') query.set('enhance', '1');
  const frame = document.createElement('iframe'); frame.title = title + ' runtime';
  frame.allow = 'cross-origin-isolated; fullscreen; autoplay'; frame.dataset.installation = query.get('installation');
  frame.onload = () => {
    try {
      frame.contentDocument.documentElement.style.backgroundColor = '#000';
      frame.contentDocument.body.style.backgroundColor = '#000';
    } catch { /* Runtime presentation remains isolated if the frame is unavailable. */ }
    try { connectRuntimeClipboard(frame.contentDocument, () => frame.contentWindow?.Module); }
    catch (error) { log('Clipboard bridge unavailable: ' + errorText(error)); }
  };
  const run = { frame, archive, generation: ++generation, renderer: null, vkReady: null, canvas: null,
    lastLog: null, firstMapped: false, hasFrame: false, failed: false, status: '', progressTimer: null, probe: null,
    startedAt: performance.now(), enhancer: null, enhancerPending: false, exe: query.get('exe') || '' };
  current = run;
  if (query.get('gpu') === '1') {
    const canvas = document.createElement('canvas'); canvas.id = 'gpuCanvas'; run.canvas = canvas; $('display').append(canvas);
    try { const { createRenderer } = await import('./webgpu.mjs'); run.renderer = await createRenderer(canvas); window.wineGpu = run.renderer; }
    catch (error) { canvas.remove(); run.canvas = null; query.set('gpu', '0'); log('Graphics fallback: ' + errorText(error)); }
  }
  if (current !== run) { run.renderer?.destroy(); run.canvas?.remove(); archive?.release(); return; }
  frame.src = 'runtime/index.html?v=__RUNTIME_VERSION__&' + query;
  $('display').append(frame); status('Starting ' + title + '. The first run may take a minute.');
  metrics = installFrameMetrics({
    tierProvider: () => (current === run ? metricsTier : null),
    open: metricsOpen(),
    onVisibilityChange: open => {
      settings.metrics = open ? 'on' : 'off';
      persistSettings();
      applyMetricsVisibility();
    },
  });
  metrics.setRunState('running');
  $('metricsHost').replaceChildren(metrics.element);
  metrics.start();
  applyMetricsVisibility();
  run.status = 'Loading the runtime and program files…';
  run.progressTimer = setInterval(() => updateStartup(run, true), 500);
}
function updateStartup(run, sample = false) {
  if (current !== run || run.failed) return;
  if (sample && run.firstMapped && !run.hasFrame) {
    try {
      run.probe ||= Object.assign(document.createElement('canvas'), { width: 32, height: 20 });
      const tierStats = metricsTier?.stats?.();
      run.hasFrame = Boolean(run.renderer?.frames || tierStats?.v2?.rendered > 0)
        || visibleFrame($('vkCanvas') || run.canvas || run.frame.contentWindow?.document.getElementById('canvas'), run.probe);
    } catch { /* The runtime document may still be navigating. */ }
  }
  if (run.hasFrame) {
    clearInterval(run.progressTimer); run.probe = null;
    $('loading').hidden = true; $('launchProgress').hidden = true;
    if (settings.enhancement !== 'off' && !run.enhancer && !run.enhancerPending) {
      run.enhancerPending = true;
      import('./enhancement.mjs').then(({ createEnhancer }) => {
        if (current !== run) return;
        run.enhancer = createEnhancer($('display'), () => $('vkCanvas') || run.canvas || run.frame.contentWindow?.document.getElementById('canvas'),
          { mode: settings.enhancement, onError: error => { log('Image enhancement disabled: ' + errorText(error)); } });
      }).catch(error => log('Image enhancement unavailable: ' + errorText(error)));
    }
    return;
  }
  const progress = startupProgress(run.status, run.lastLog, run.hasFrame, performance.now(), run.startedAt, run.firstMapped);
  const waitingForFrame = progress.title === 'Waiting for the first frame';
  $('loading').classList.toggle('waiting-for-frame', waitingForFrame);
  $('loadingTitle').textContent = progress.title;
  status(waitingForFrame
    ? 'The program is running. Large games can take several minutes to show their first frame.'
    : progress.detail);
  $('loadingActivity').textContent = progress.activity;
  $('loadingActivity').hidden = waitingForFrame || !progress.activity;
  $('loadingElapsed').textContent = progress.elapsed;
}
function startupError(run, value) {
  run.failed = true; clearInterval(run.progressTimer);
  showPlayerError(value);
}
async function runSaved(id, signal) {
  let pkg = await getPackage(id);
  if (!pkg) throw Error('This folder is no longer saved. Upload it again.');
  pkg = await openDirectPackage(pkg, { signal });
  if (pkg.mode === 'direct' && !pkg.files.some(file => file.path === pkg.program?.exe)) pkg.program = await detectProgram(pkg.files, { name: pkg.name });
  await runPackage(pkg, signal);
}
async function runPackage(pkg, signal, prepared = null) {
  await closeRuntime(); signal?.throwIfAborted(); preparing(pkg.name);
  status('Detecting the program…');
  const program = pkg.program || await detectProgram(pkg.files, { name: pkg.name });
  if (!pkg.program) { pkg = { ...pkg, program }; await savePackage(pkg); }
  let archive = prepared;
  try {
    if (!archive && (pkg.mode === 'direct' || pkg.files.reduce((sum, file) => sum + file.size, 0) > 128 * 1024 ** 2 || pkg.files.length > limits.count)) {
      archive = await archiveFor(pkg, signal);
    }
    if (pkg.mode === 'direct') await savePackage(pkg);
    signal?.throwIfAborted();
    const query = runtimeParameters(program, { installation: pkg.id, archive: archive?.url, archiveBytes: archive?.bytes });
    await launchFrame(query, pkg.name, archive);
  } catch (error) { archive?.release(); throw error; }
}
async function archiveFor(pkg, signal) {
  status('Preparing the folder for streamed loading…'); $('launchProgress').hidden = false;
  return prepareArchive(pkg, { signal, onProgress: (done, total) => {
    $('launchProgress').max = total || 1; $('launchProgress').value = done;
    status((pkg.mode === 'direct' ? 'Indexing originals (no copy): ' : 'Preparing files: ') + formatBytes(done) + ' of ' + formatBytes(total));
  } });
}
async function importFolder(files, name, signal, directory = null) {
  let archive, pkg, owned = files, saved = false;
  try {
    await closeRuntime(); signal.throwIfAborted(); preparing(name || 'Your folder'); status('Finding the main program…');
    checkEntries(files, folderLimits);
    const program = await detectProgram(files, { name }); signal.throwIfAborted();
    if (!directory) {
      status('Copying the folder into browser storage…');
      navigator.storage?.persist?.().catch(() => {});
      owned = await ownFiles(files, { signal, onProgress: (done, total) => status('Saving files: ' + formatBytes(done) + ' of ' + formatBytes(total)) });
    }
    pkg = { id: crypto.randomUUID(), name: name || program.exe.split('/').at(-1).replace(/\.exe$/i, ''), files: owned, mode: directory ? 'direct' : 'copy', directory, sourceDirectory: sourceDirectory(owned), program, created: new Date().toISOString() };
    if (directory || files.reduce((sum, file) => sum + file.size, 0) > 128 * 1024 ** 2 || files.length > limits.count) archive = await archiveFor(pkg, signal);
    signal.throwIfAborted(); status('Saving this folder on your device…'); await savePackage(pkg); saved = true; retainExtracted(owned);
    lastAction = nextSignal => runSaved(pkg.id, nextSignal);
    navigator.storage?.persist?.().catch(() => {});
    await refreshLibrary(); signal.throwIfAborted();
    await runPackage(pkg, signal, archive);
  } catch (error) { archive?.release(); if (pkg && !saved) await removeArchive(pkg).catch(() => {}); throw error; }
  finally { await cleanupExtracted(owned).catch(() => {}); if (owned !== files) await cleanupExtracted(files).catch(() => {}); }
}
async function startDemo(demo, signal) {
  const launcherWindow = /** @type {Window & { AudioContext?: typeof AudioContext; webkitAudioContext?: typeof AudioContext; bwLauncherAudioContext?: AudioContext; bwLauncherGesturePending?: boolean }} */ (window);
  const AudioContextCtor = launcherWindow.AudioContext || launcherWindow.webkitAudioContext;
  if (AudioContextCtor) {
    try {
      const context = new AudioContextCtor();
      launcherWindow.bwLauncherAudioContext = context;
      if (context.state === 'suspended') context.resume().catch(error => {
        console.warn('Launcher audio context resume failed:', error?.name || 'failed');
      });
    } catch (error) {
      console.warn('Launcher audio context setup failed:', error?.name || 'failed');
    }
  }
  await closeRuntime(); signal.throwIfAborted(); preparing(demo.title); status('Opening ' + demo.title + '…');
  const query = await demoParameters(demo); signal.throwIfAborted();
  launcherWindow.bwLauncherGesturePending = true;
  await launchFrame(query, demo.title);
}
function renderDemos() {
  const fragment = document.createDocumentFragment();
  for (const demo of demos) {
    const button = document.createElement('button'); button.type = 'button'; button.className = 'program-card app-card';
    button.dataset.game = demo.id; button.dataset.launch = ''; button.setAttribute('aria-label', 'Run ' + demo.title);
    const title = text('span', '', 'app-title');
    const glyph = icon(demo.icon); glyph.classList.add('app-icon');
    title.append(glyph, text('span', demo.title));
    if (demo.size) title.append(text('span', formatBytes(demo.size), 'badge'));
    button.append(title, text('span', demo.description, 'app-desc'));
    button.onclick = () => action(signal => startDemo(demo, signal)); fragment.append(button);
  }
  $('gameCatalog').replaceChildren(fragment); $('gameCatalog').removeAttribute('aria-busy');
}
async function refreshLibrary() {
  const packages = await listPackages(); const fragment = document.createDocumentFragment();
  packages.sort((a, b) => String(b.created).localeCompare(String(a.created)));
  for (const pkg of packages) {
    const item = text('article', '', 'saved-item app-card');
    const copy = text('div', '', 'saved-copy');
    copy.append(text('h3', pkg.name, 'saved-title'));
    const metadata = [];
    const arch = pkg.program?.arch;
    if (arch === 'x86' || arch === 'i386') metadata.push('32-bit');
    else if (arch === 'x64' || arch === 'x86_64' || arch === 'amd64') metadata.push('64-bit');
    if (pkg.files.length && pkg.files.every(file => Number.isFinite(file.size))) {
      metadata.push(formatBytes(pkg.files.reduce((total, file) => total + file.size, 0)));
    }
    if (metadata.length) copy.append(text('p', metadata.join(' · '), 'saved-metadata'));
    const actions = text('div', '', 'saved-actions');
    const run = document.createElement('button'); run.type = 'button'; run.className = 'saved-run button primary'; run.dataset.launch = ''; run.textContent = 'Play';
    run.onclick = () => action(signal => runSaved(pkg.id, signal));
    const manage = document.createElement('button'); manage.type = 'button'; manage.className = 'icon-button saved-more';
    manage.setAttribute('aria-label', 'More for ' + pkg.name); manage.title = 'More'; manage.append(icon('dots'));
    manage.onclick = () => action(() => manageFolder(pkg.id), { retry: null });
    actions.append(run, manage); item.append(copy, actions); fragment.append(item);
  }
  if (!packages.length) fragment.append(text('p', 'Folders you open appear here.', 'empty'));
  $('library').replaceChildren(fragment); $('library').removeAttribute('aria-busy');
  $('folderCount').textContent = packages.length ? packages.length.toLocaleString() + ' saved' : '';
}
async function manageFolder(id) {
  selected = await getPackage(id); if (!selected) throw Error('Folder no longer exists.');
  const program = selected.program || await detectProgram(selected.files, { name: selected.name });
  $('folderTitle').textContent = selected.name;
  $('folderInfo').textContent = selected.mode === 'direct' ? 'Linked folder. Originals are read-only; saves and patches use browser storage. Restoring a backup creates a browser copy.' : 'Browser copy. The original folder is no longer needed.';
  $('folderInfo').hidden = false; $('relink').hidden = selected.mode !== 'direct';
  $('executable').replaceChildren();
  for (const file of selected.files) if (/\.exe$/i.test(file.path)) $('executable').add(new Option(file.path, file.path));
  $('executable').value = program.exe; $('arguments').value = JSON.stringify(program.args);
  $('folderError').hidden = true; $('folderDialog').showModal();
}
window.addEventListener('message', event => {
  const run = current;
  if (!run || event.origin !== location.origin || event.source !== run.frame.contentWindow || event.data?.wine !== true) return;
  const { type, data } = event.data;
  if (type === 'status') { run.status = String(data); const failure = startupFailure(run.status, run.exe); if (failure) startupError(run, failure); else updateStartup(run); }
  if (type === 'error') startupError(run, String(data));
  if (type === 'mouse-capture') {
    $('captureMouse').title = data.locked || data.armed ? 'Release mouse' : 'Capture mouse';
    $('captureMouse').setAttribute('aria-label', $('captureMouse').title);
    $('captureMouse').setAttribute('aria-pressed', String(Boolean(data.locked || data.armed)));
    const hint = data.error || (data.armed ? 'Click inside the game to capture the mouse. Press Esc to release it.' : data.locked ? 'Mouse captured. Press Esc to release it.' : '');
    $('mouseCaptureHint').textContent = hint; $('mouseCaptureHint').hidden = !hint;
  }
  if (type === 'size' && Number.isFinite(data?.width) && Number.isFinite(data?.height) && data.width > 0 && data.height > 0) {
    $('display').style.setProperty('--program-aspect', `${data.width} / ${data.height}`);
  }
  if (type === 'log') {
    // The live metrics snapshot is a machine-readable line, not user-facing
    // output: it feeds the panel and never enters the log pane.
    if (metrics && current === run && String(data).includes(METRICS_LINE_PREFIX) && metrics.feed(String(data))) {
      run.lastLog = performance.now();
      return;
    }
    log(String(data));
    const failure = startupFailure(String(data), run.exe);
    if (!run.hasFrame && failure) startupError(run, failure);
    run.lastLog = performance.now();
    if (String(data).includes('first window mapped')) {
      run.firstMapped = true;
      const runtime = /** @type {(Window & { bwForwardLauncherAudioGesture?: () => boolean }) | null} */ (run.frame.contentWindow);
      const canvas = runtime?.document.getElementById('canvas');
      canvas?.focus({ preventScroll: true });
      const launcherWindow = /** @type {Window & { bwLauncherGesturePending?: boolean }} */ (window);
      if (launcherWindow.bwLauncherGesturePending) {
        launcherWindow.bwLauncherGesturePending = false;
        runtime?.bwForwardLauncherAudioGesture?.();
      }
    }
    updateStartup(run);
  }
  if (type === 'gpu' && run.renderer) {
    try { run.renderer.render(data); run.firstMapped = true; updateStartup(run, true); } catch (error) { log('Graphics fallback: ' + errorText(error)); run.renderer.destroy(); run.renderer = null; $('gpuCanvas')?.remove(); run.canvas = null; }
  }
  if (type === 'vk' || type === 'vk2') {
    if (!run.vkReady) {
      run.vkReady = import('./vkwebgpu.mjs').then(module => {
        if (current !== run) return null;
        return (window.bwVkTier = metricsTier = module.installVkPageTier());
      });
      window.bwVkTierReady = run.vkReady;
    }
    run.vkReady.then(async tier => {
      if (current !== run || !tier) return;
      await (type === 'vk' ? tier.frame(data) : tier.chunk(data.bytes, data.flags));
      run.firstMapped = true; updateStartup(run, true);
      const canvas = $('vkCanvas');
      if (canvas?.width && canvas?.height) $('display').style.setProperty('--program-aspect', `${canvas.width} / ${canvas.height}`);
    }).catch(error => { if (current === run) log('Graphics fallback: ' + errorText(error)); });
  }
});
$('folderButton').onclick = () => {
  if (settings.folderMode === 'copy') return $('folderInput').click();
  action(async signal => {
    if (!window.showDirectoryPicker) throw Error('File system access is unavailable. Select Copy to browser, or open this page in Chrome or Edge.');
    let directory;
    try { directory = await window.showDirectoryPicker({ mode: 'read', id: 'winbox-programs' }); }
    catch (error) { if (error.name === 'AbortError') return; throw error; }
    preparing(directory.name); status('Reading the folder index…');
    await importFolder(await readDirectory(directory, { signal }), directory.name, signal, directory);
  }, { retry: null });
};
$('relink').onclick = () => action(async signal => {
  let directory;
  try { directory = await window.showDirectoryPicker({ mode: 'read', id: 'winbox-programs' }); }
  catch (error) { if (error.name === 'AbortError') return; throw error; }
  const files = await readDirectory(directory, { signal });
  const program = await detectProgram(files, { name: selected.name });
  selected = { ...selected, directory, files, program }; await savePackage(selected);
  await refreshLibrary(); $('folderDialog').close(); message('Folder relinked. Your browser saves are unchanged.');
}, { retry: null });
$('zipButton').onclick = () => $('zipInput').click();
$('folderInput').onchange = () => {
  const chosen = Array.from(/** @type {FileList} */ ($('folderInput').files)); if (!chosen.length) return;
  const name = chosen[0].webkitRelativePath.split('/')[0] || 'Your folder'; $('folderInput').value = '';
  action(async signal => importFolder(await fromFiles(chosen), name, signal));
};
$('zipInput').onchange = () => {
  const file = $('zipInput').files[0]; if (!file) return; $('zipInput').value = '';
  action(async signal => { preparing(file.name); status('Reading the package…'); return importFolder(/\.zip$/i.test(file.name) ? await fromZip(file, { budget: folderLimits, stream: true, signal }) : await fromFiles([file]), file.name.replace(/\.(zip|exe)$/i, ''), signal); });
};
let dragDepth = 0;
window.addEventListener('dragenter', event => { if (event.dataTransfer?.types.includes('Files')) { event.preventDefault(); dragDepth++; $('dropOverlay').hidden = false; } });
window.addEventListener('dragover', event => { if (event.dataTransfer?.types.includes('Files')) event.preventDefault(); });
window.addEventListener('dragleave', () => { if (--dragDepth <= 0) { dragDepth = 0; $('dropOverlay').hidden = true; } });
window.addEventListener('drop', event => {
  event.preventDefault(); dragDepth = 0; $('dropOverlay').hidden = true;
  if (!event.dataTransfer || busy) return;
  const items = Array.from(event.dataTransfer.items), files = Array.from(event.dataTransfer.files);
  const singleProgramFile = files.length === 1 && /\.(zip|exe)$/i.test(files[0].name);
  if (settings.folderMode === 'direct' && !singleProgramFile) { message('To open a dropped folder, turn off Read folders in place in Settings.'); return; }
  action(async signal => {
    const name = files[0]?.name || 'Your folder';
    preparing(name); status('Reading the package…');
    const entries = singleProgramFile
      ? (/\.zip$/i.test(files[0].name) ? await fromZip(files[0], { budget: folderLimits, stream: true, signal }) : await fromFiles(files))
      : items.some(item => item.webkitGetAsEntry?.()) ? await fromDrop(items) : await fromFiles(files);
    return importFolder(entries, name.replace(/\.(zip|exe)$/i, '') || 'Dropped folder', signal);
  });
});
$('stop').onclick = stop; $('back').onclick = stop;
$('restart').onclick = () => lastAction && action(lastAction);
$('retry').onclick = () => lastAction && action(lastAction);
$('playerRetry').onclick = () => lastAction && action(lastAction);
$('playerShowLog').onclick = () => {
  const diagnostics = $('log').closest('details');
  if (!diagnostics) return;
  diagnostics.open = true;
  diagnostics.scrollIntoView({ block: 'nearest' });
  diagnostics.querySelector('summary')?.focus({ preventScroll: true });
};
$('stats').onclick = () => { settings.metrics = metricsOpen() ? 'off' : 'on'; persistSettings(); applyMetricsVisibility(); };
$('captureMouse').onclick = () => {
  try {
    current?.frame.contentWindow?.bwSetMouseCapture($('captureMouse').getAttribute('aria-pressed') !== 'true');
    current?.frame.contentWindow?.document.getElementById('canvas')?.focus({ preventScroll: true });
  } catch (error) { $('mouseCaptureHint').textContent = errorText(error); $('mouseCaptureHint').hidden = false; }
};
$('fullscreen').onclick = () => $('display').requestFullscreen().catch(error => message(errorText(error), true));
$('settingsButton').onclick = () => $('settingsDialog').showModal();
$('clearShaderCache').onclick = async () => {
  try { const { persistentShaderCache } = await import('./shader-cache.mjs'); await persistentShaderCache.clear(); message('Shader cache cleared.'); }
  catch { message('Unable to clear the shader cache.', true); }
};
for (const dialog of document.querySelectorAll('dialog')) dialog.addEventListener('click', event => { if (event.target === dialog) { const box = dialog.getBoundingClientRect(); if (event.clientX < box.left || event.clientX > box.right || event.clientY < box.top || event.clientY > box.bottom) dialog.close(); } });
$('start').onclick = () => action(async signal => {
  selected = await openDirectPackage(selected, { signal });
  const exe = safePath($('executable').value), file = selected.files.find(file => file.path === exe);
  const base = selected.program || await detectProgram(selected.files, { name: selected.name });
  const program = { ...base, exe, arch: await peArchitecture(file.blob), args: JSON.parse($('arguments').value) };
  runtimeParameters(program); selected = { ...selected, program }; await savePackage(selected);
  $('folderDialog').close(); await runPackage(selected, signal);
});
function downloadUrl(url, name) { const link = document.createElement('a'); link.href = url; link.download = name; link.click(); }
$('patch').onclick = () => $('patchInput').click();
$('patchInput').onchange = () => action(async signal => {
  const file = $('patchInput').files[0]; if (!file || !selected) return;
  const patch = await fromZip(file, { strip: false, signal }); await closeRuntime();
  const merged = new Map(selected.files.map(file => [file.path.toLowerCase(), file]));
  for (const file of patch) merged.set(file.path.toLowerCase(), selected.mode === 'direct' ? { ...file, override: true } : file);
  const files = [...merged.values()]; checkEntries(files, folderLimits);
  const program = await detectProgram(files, { name: selected.name });
  const saves = new Map((await storedFiles(selected.id)).map(file => [file.path, file]));
  for (const file of await readSaveBlobs(patch, '/root/home/username/apps/')) saves.set(file.path, file);
  const previous = selected;
  selected = await replaceInstallation(previous, { files, program }, [...saves.values()]);
  await removeArchive(previous).catch(() => {});
  await refreshLibrary(); $('patchInput').value = ''; $('folderDialog').close(); message('Patch applied. Open the folder to run it.');
}, { retry: null });
$('export').onclick = () => action(async () => {
  selected = await openDirectPackage(selected);
  await snapshotRuntime(current?.frame); const stored = await storedFiles(selected.id);
  const files = selected.files.map(file => ({ ...file, path: 'package/' + file.path }));
  for (const entry of stored) {
    if (!entry.path.startsWith('/root/home/username/')) throw Error('Invalid save path');
    files.push({ path: 'writable/' + safePath(entry.path.slice('/root/home/username/'.length)), size: entry.data.length, blob: new Blob([entry.data]) });
  }
  const backup = { id: crypto.randomUUID(), created: new Date().toISOString(), files };
  const archive = await prepareArchive(backup, { prefix: '', onProgress: (done, total) => { $('folderInfo').hidden = false; $('folderInfo').textContent = 'Preparing backup: ' + formatBytes(done) + ' of ' + formatBytes(total); } });
  downloadUrl(archive.url, selected.name + '-backup.zip');
  setTimeout(() => { archive.release(); removeArchive(backup).catch(() => {}); }, 60000);
  message('Backup downloaded, including files and local saves.');
}, { retry: null });
$('restore').onclick = () => $('restoreInput').click();
$('restoreInput').onchange = () => action(async signal => {
  const file = $('restoreInput').files[0]; if (!file || !selected) return;
  $('folderInfo').hidden = false; $('folderInfo').textContent = 'Reading the backup…';
  const entries = await fromZip(file, { strip: false, budget: folderLimits, stream: true, signal });
  try {
  if (entries.some(entry => !entry.path.startsWith('package/') && !entry.path.startsWith('writable/'))) throw Error('Choose a backup created by this launcher.');
  const files = entries.filter(entry => entry.path.startsWith('package/')).map(entry => ({ ...entry, path: safePath(entry.path.slice(8)) }));
  checkEntries(files, folderLimits); const program = await detectProgram(files, { name: selected.name });
  const writable = entries.filter(entry => entry.path.startsWith('writable/')).map(entry => ({ ...entry, path: safePath(entry.path.slice(9)) }));
  const saves = await readSaveBlobs(writable, '/root/home/username/');
  await closeRuntime(); const previous = selected;
  selected = await replaceInstallation(previous, { files, program, mode: 'copy', directory: null, sourceDirectory: sourceDirectory(entries) }, saves);
  retainExtracted(entries);
  await removeSource(previous).catch(() => {});
  await removeArchive(previous).catch(() => {});
  await refreshLibrary(); $('restoreInput').value = ''; $('folderDialog').close(); message('Backup restored. Open the folder to run it.');
  } finally { await cleanupExtracted(entries).catch(() => {}); }
}, { retry: null });
$('remove').onclick = () => action(async () => {
  if (!selected || !confirm('Delete this folder and its saves from this browser?')) return;
  await closeRuntime(); const pkg = selected;
  await new Promise((resolve, reject) => { const request = indexedDB.deleteDatabase('bw64persist-wine-' + pkg.id); request.onsuccess = resolve; request.onerror = () => reject(request.error); request.onblocked = () => reject(Error('Close other tabs running this folder, then try again.')); });
  await deletePackage(pkg.id); await removeArchive(pkg); await removeSource(pkg).catch(() => {}); selected = null;
  await refreshLibrary(); $('folderDialog').close(); message('Folder deleted from this browser.');
}, { retry: null });
window.wineLibrary = { getPackage, listPackages };
window.bwReadLocalArchive = (url, start, end) => {
  if (current?.archive?.url !== url || !current.archive.read) throw Error('This local folder is no longer open. Restart it from the library.');
  return current.archive.read(start, end);
};
window.wineGames = { catalog: demos, install: id => action(signal => startDemo(demos.find(demo => demo.id === id), signal)) };
renderDemos();
const supported = crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined';
if (!supported) message('This browser needs cross-origin isolation to run programs. Reload the page, or open it in a current browser.', true);
await refreshLibrary().catch(error => { $('library').replaceChildren(text('p', 'Saved folders could not be loaded. Reload to try again.', 'empty')); $('library').removeAttribute('aria-busy'); message(errorText(error), true); });
