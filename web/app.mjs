import { fromFiles, fromDrop, fromZip, cleanupExtracted, ownFiles, sourceDirectory, retainExtracted, removeSource, peArchitecture, safePath, checkEntries, folderLimits, limits } from './files.mjs';
import { listPackages, getPackage, savePackage, deletePackage } from './storage.mjs';
import { detectProgram, runtimeParameters } from './launch.mjs';
import { demos, demoParameters } from './demos.mjs';
import { formatBytes } from './games.mjs';
import { prepareArchive, removeArchive } from './package-archive.mjs';
import { storedFiles, snapshotRuntime, readSaveBlobs } from './session-storage.mjs';
import { replaceInstallation } from './installation.mjs';
import { startupProgress, visibleFrame } from './startup.mjs';

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
const settings = { appearance: 'system', graphics: 'auto', internet: 'off', enhancement: 'off', sharpness: 0.5 };
try { Object.assign(settings, JSON.parse(localStorage.getItem('wine.launcher.settings.v1') || '{}')); } catch {}
function appearance(value) {
  document.documentElement.dataset.theme = ['light', 'dark'].includes(value) ? value : 'system';
  $('appearance').value = value;
  const dark = value === 'dark' || (value === 'system' && matchMedia('(prefers-color-scheme: dark)').matches);
  document.querySelector('meta[name="theme-color"]').setAttribute('content', dark ? '#141518' : '#f5f6f8');
}
appearance(settings.appearance);
$('backend').value = settings.graphics;
for (const name of ['internet', 'enhancement', 'sharpness']) {
  $(name).value = settings[name];
  $(name).onchange = () => { settings[name] = name === 'sharpness' ? Number($(name).value) : $(name).value; persistSettings(); };
}
$('appearance').onchange = () => { settings.appearance = $('appearance').value; appearance(settings.appearance); persistSettings(); };
$('backend').onchange = () => { settings.graphics = $('backend').value; persistSettings(); };
function persistSettings() { try { localStorage.setItem('wine.launcher.settings.v1', JSON.stringify(settings)); } catch {} }
function message(value, error = false) {
  $('notice').hidden = !value; $('noticeText').textContent = value; $('notice').classList.toggle('error', error);
}
function status(value) { $('status').textContent = value; }
function log(value) { $('log').textContent = ($('log').textContent + '\n' + value).slice(-48000); }
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
  $('retry').hidden = true; $('playerRetry').hidden = true;
  try { await fn(active.signal); }
  catch (error) {
    if (active.signal.aborted) return;
    const value = errorText(error); message(value, true);
    if (!$('player').hidden) { $('loading').hidden = false; $('loadingTitle').textContent = 'Could not start this program'; status(value); $('loadingActivity').hidden = true; $('playerRetry').hidden = !lastAction; }
    $('retry').hidden = !lastAction;
    if ($('folderDialog').open) { $('folderError').hidden = false; $('folderError').textContent = value; }
  } finally { if (operation === active) operation = null; lock(false); }
}
function preparing(title) {
  $('launcher').hidden = true; $('player').hidden = false;
  $('runtimeTitle').textContent = title;
  $('loadingTitle').textContent = 'Getting things ready'; $('loading').hidden = false;
  $('launchProgress').hidden = true; $('playerRetry').hidden = true;
  $('loadingActivity').hidden = true; $('loadingElapsed').textContent = '';
  $('display').style.removeProperty('--program-aspect');
  $('log').textContent = ''; message('');
  $('runtimeTitle').setAttribute('tabindex', '-1'); $('runtimeTitle').focus({ preventScroll: true });
}
async function closeRuntime() {
  const run = current; current = null; generation++;
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
    message(failure ? errorText(failure) : '', Boolean(failure));
    $('folderButton').focus({ preventScroll: true });
  })();
  lock(busy);
  try { await stopping; } finally { stopping = null; lock(busy); }
}
async function launchFrame(query, title, archive = null) {
  if (settings.internet === 'on') { query.set('network', '1'); query.set('wisp', '1'); }
  if (settings.enhancement !== 'off') query.set('enhance', '1');
  const frame = document.createElement('iframe'); frame.title = title + ' runtime';
  frame.allow = 'cross-origin-isolated; fullscreen'; frame.dataset.installation = query.get('installation');
  const run = { frame, archive, generation: ++generation, renderer: null, vkReady: null, canvas: null,
    lastLog: null, firstMapped: false, hasFrame: false, failed: false, status: '', progressTimer: null, probe: null,
    startedAt: performance.now(), enhancer: null, enhancerPending: false };
  current = run;
  if (query.get('gpu') === '1') {
    const canvas = document.createElement('canvas'); canvas.id = 'gpuCanvas'; run.canvas = canvas; $('display').append(canvas);
    try { const { createRenderer } = await import('./webgpu.mjs'); run.renderer = await createRenderer(canvas); window.wineGpu = run.renderer; }
    catch (error) { canvas.remove(); run.canvas = null; query.set('gpu', '0'); log('Graphics fallback: ' + errorText(error)); }
  }
  if (current !== run) { run.renderer?.destroy(); run.canvas?.remove(); archive?.release(); return; }
  frame.src = 'runtime/index.html?v=__RUNTIME_VERSION__&' + query;
  $('display').append(frame); status('Starting ' + title + '. The first run may take a minute.');
  run.status = 'Loading the runtime and program files…';
  run.progressTimer = setInterval(() => updateStartup(run, true), 500);
}
function updateStartup(run, sample = false) {
  if (current !== run || run.failed) return;
  if (sample && run.firstMapped && !run.hasFrame) {
    try {
      run.probe ||= Object.assign(document.createElement('canvas'), { width: 32, height: 20 });
      run.hasFrame = Boolean(run.renderer?.frames) || visibleFrame($('vkCanvas') || run.frame.contentWindow?.document.getElementById('canvas'), run.probe);
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
          { mode: settings.enhancement, sharpness: settings.sharpness, onError: error => { log('Image enhancement disabled: ' + errorText(error)); } });
      }).catch(error => log('Image enhancement unavailable: ' + errorText(error)));
    }
    return;
  }
  const progress = startupProgress(run.status, run.lastLog, run.hasFrame, performance.now(), run.startedAt, run.firstMapped);
  $('loadingTitle').textContent = progress.title; status(progress.detail);
  $('loadingActivity').textContent = progress.activity; $('loadingActivity').hidden = !progress.activity;
  $('loadingElapsed').textContent = progress.elapsed;
}
async function runSaved(id, signal) {
  const pkg = await getPackage(id);
  if (!pkg) throw Error('This folder is no longer saved. Upload it again.');
  await runPackage(pkg, signal);
}
async function runPackage(pkg, signal, prepared = null) {
  await closeRuntime(); signal?.throwIfAborted(); preparing(pkg.name);
  status('Detecting the program…');
  const program = pkg.program || await detectProgram(pkg.files, { name: pkg.name });
  if (!pkg.program) { pkg = { ...pkg, program }; await savePackage(pkg); }
  let archive = prepared;
  try {
    if (!archive && (pkg.files.reduce((sum, file) => sum + file.size, 0) > 128 * 1024 ** 2 || pkg.files.length > limits.count)) {
      archive = await archiveFor(pkg, signal);
    }
    signal?.throwIfAborted();
    const query = runtimeParameters(program, { installation: pkg.id, archive: archive?.url, archiveBytes: archive?.bytes,
      graphics: settings.graphics === 'auto' ? program.graphics : settings.graphics });
    await launchFrame(query, pkg.name, archive);
  } catch (error) { archive?.release(); throw error; }
}
async function archiveFor(pkg, signal) {
  status('Preparing the folder for streamed loading…'); $('launchProgress').hidden = false;
  return prepareArchive(pkg, { signal, onProgress: (done, total) => {
    $('launchProgress').max = total || 1; $('launchProgress').value = done;
    status('Preparing files: ' + formatBytes(done) + ' of ' + formatBytes(total));
  } });
}
async function importFolder(files, name, signal) {
  let archive, pkg, owned = files, saved = false;
  try {
    await closeRuntime(); signal.throwIfAborted(); preparing(name || 'Your folder'); status('Finding the main program…');
    checkEntries(files, folderLimits);
    const program = await detectProgram(files, { name }); signal.throwIfAborted();
    status('Copying the folder into browser storage…');
    owned = await ownFiles(files, { signal, onProgress: (done, total) => status('Saving files: ' + formatBytes(done) + ' of ' + formatBytes(total)) });
    pkg = { id: crypto.randomUUID(), name: name || program.exe.split('/').at(-1).replace(/\.exe$/i, ''), files: owned, sourceDirectory: sourceDirectory(owned), program, created: new Date().toISOString() };
    if (files.reduce((sum, file) => sum + file.size, 0) > 128 * 1024 ** 2 || files.length > limits.count) archive = await archiveFor(pkg, signal);
    signal.throwIfAborted(); status('Saving this folder on your device…'); await savePackage(pkg); saved = true; retainExtracted(owned);
    lastAction = nextSignal => runSaved(pkg.id, nextSignal);
    navigator.storage?.persist?.().catch(() => {});
    await refreshLibrary(); signal.throwIfAborted();
    await runPackage(pkg, signal, archive);
  } catch (error) { archive?.release(); if (pkg && !saved) await removeArchive(pkg).catch(() => {}); throw error; }
  finally { await cleanupExtracted(owned).catch(() => {}); if (owned !== files) await cleanupExtracted(files).catch(() => {}); }
}
async function startDemo(demo, signal) {
  await closeRuntime(); signal.throwIfAborted(); preparing(demo.title); status('Opening ' + demo.title + '…');
  const query = await demoParameters(demo); signal.throwIfAborted();
  if (settings.graphics !== 'auto') query.set('gpu', settings.graphics === 'webgpu' ? '1' : '0');
  await launchFrame(query, demo.title);
}
function renderDemos() {
  const fragment = document.createDocumentFragment();
  for (const demo of demos) {
    const button = document.createElement('button'); button.type = 'button'; button.className = 'program-card';
    button.dataset.game = demo.id; button.dataset.launch = ''; button.setAttribute('aria-label', 'Run ' + demo.title);
    const play = icon('player-play'); play.classList.add('card-play');
    button.append(icon(demo.icon), text('strong', demo.title), play);
    button.onclick = () => action(signal => startDemo(demo, signal)); fragment.append(button);
  }
  $('gameCatalog').replaceChildren(fragment); $('gameCatalog').removeAttribute('aria-busy');
}
async function refreshLibrary() {
  const packages = await listPackages(); const fragment = document.createDocumentFragment();
  packages.sort((a, b) => String(b.created).localeCompare(String(a.created)));
  for (const pkg of packages) {
    const item = text('div', '', 'saved-item');
    const run = document.createElement('button'); run.type = 'button'; run.className = 'saved-run'; run.dataset.launch = '';
    const copy = text('span', '', 'saved-copy');
    copy.append(text('strong', pkg.name), text('span', pkg.files.length.toLocaleString() + ' files · ' + formatBytes(pkg.files.reduce((n, f) => n + f.size, 0))));
    run.append(icon('folder'), copy); run.onclick = () => action(signal => runSaved(pkg.id, signal));
    const manage = document.createElement('button'); manage.type = 'button'; manage.className = 'icon-button';
    manage.setAttribute('aria-label', 'Settings for ' + pkg.name); manage.append(icon('settings')); manage.onclick = () => action(() => manageFolder(pkg.id), { retry: null });
    item.append(run, manage); fragment.append(item);
  }
  if (!packages.length) fragment.append(text('p', 'Uploaded folders appear here, ready for next time.', 'empty'));
  $('library').replaceChildren(fragment); $('library').removeAttribute('aria-busy');
  $('folderCount').textContent = packages.length ? packages.length.toLocaleString() + ' saved' : '';
}
async function manageFolder(id) {
  selected = await getPackage(id); if (!selected) throw Error('Folder no longer exists.');
  const program = selected.program || await detectProgram(selected.files, { name: selected.name });
  $('folderTitle').textContent = selected.name; $('folderInfo').textContent = ''; $('folderInfo').hidden = true;
  $('executable').replaceChildren();
  for (const file of selected.files) if (/\.exe$/i.test(file.path)) $('executable').add(new Option(file.path, file.path));
  $('executable').value = program.exe; $('arguments').value = JSON.stringify(program.args);
  $('folderError').hidden = true; $('folderDialog').showModal();
}
window.addEventListener('message', event => {
  const run = current;
  if (!run || event.origin !== location.origin || event.source !== run.frame.contentWindow || event.data?.wine !== true) return;
  const { type, data } = event.data;
  if (type === 'status') { run.status = String(data); updateStartup(run); }
  if (type === 'error') { run.failed = true; clearInterval(run.progressTimer); $('loading').hidden = false; $('loadingTitle').textContent = 'The program could not continue'; status(String(data)); $('loadingActivity').hidden = true; $('playerRetry').hidden = !lastAction; }
  if (type === 'size' && Number.isFinite(data?.width) && Number.isFinite(data?.height) && data.width > 0 && data.height > 0) {
    $('display').style.setProperty('--program-aspect', `${data.width} / ${data.height}`);
  }
  if (type === 'log') {
    log(String(data));
    run.lastLog = performance.now();
    if (String(data).includes('first window mapped')) {
      run.firstMapped = true;
      run.frame.contentWindow?.document.getElementById('canvas')?.focus({ preventScroll: true });
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
        return (window.bwVkTier = module.installVkPageTier());
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
$('folderButton').onclick = () => $('folderInput').click();
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
  action(async signal => { preparing(files[0]?.name || 'Your folder'); status('Reading the package…'); return importFolder(files.length === 1 && /\.zip$/i.test(files[0].name) ? await fromZip(files[0], { budget: folderLimits, stream: true, signal }) : items.some(item => item.webkitGetAsEntry?.()) ? await fromDrop(items) : await fromFiles(files), files[0]?.name.replace(/\.(zip|exe)$/i, '') || 'Dropped folder', signal); });
});
$('stop').onclick = stop; $('back').onclick = stop;
$('restart').onclick = () => lastAction && action(lastAction);
$('retry').onclick = () => lastAction && action(lastAction);
$('playerRetry').onclick = () => lastAction && action(lastAction);
$('fullscreen').onclick = () => $('display').requestFullscreen().catch(error => message(errorText(error), true));
$('settingsButton').onclick = () => $('settingsDialog').showModal();
for (const dialog of document.querySelectorAll('dialog')) dialog.addEventListener('click', event => { if (event.target === dialog) { const box = dialog.getBoundingClientRect(); if (event.clientX < box.left || event.clientX > box.right || event.clientY < box.top || event.clientY > box.bottom) dialog.close(); } });
$('start').onclick = () => action(async signal => {
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
  for (const file of patch) merged.set(file.path.toLowerCase(), file);
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
  selected = await replaceInstallation(previous, { files, program, sourceDirectory: sourceDirectory(entries) }, saves);
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
window.wineGames = { catalog: demos, install: id => action(signal => startDemo(demos.find(demo => demo.id === id), signal)) };
renderDemos();
const supported = crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined';
if (!supported) message('This browser needs cross-origin isolation to run programs. Reload the page, or open it in a current browser.', true);
await refreshLibrary().catch(error => { $('library').replaceChildren(text('p', 'Saved folders could not be loaded. Reload to try again.', 'empty')); $('library').removeAttribute('aria-busy'); message(errorText(error), true); });
