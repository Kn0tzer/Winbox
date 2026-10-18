// web/tests/frame-metrics-probe.mjs — live frame-stage metrics, real browser.
//
// Validates the two halves the unit tests cannot: real DOM/CSS/app-shell wiring
// and the page tier's genuine GPU timing.
//
//   PHASE 1  page tier on the real D3D9/DXVK path (tri9pong through the vk64
//            bridge). Asserts window.bwVkTier.metrics() reports measured present
//            pacing, translate/pipeline/submit/render spans and a GPU verdict of
//            measured | inactive | unavailable WITH a reason — never a submit
//            time standing in for GPU execution.
//   PHASE 2  the app shell's live panel: launch the OpenGL cube demo, feed a
//            runtime snapshot line through the same message channel the guest
//            prints on, and assert the panel renders measured rows, the Stats
//            button toggles it, and no console/page errors appear.
//
// The served tree is a CANDIDATE EXPORT assembled in /tmp: the installed dist
// (runtime binaries, guest zips) plus this worktree's web/ JS/CSS/HTML. No
// emulator rebuild is required or performed.
//
// Run from this worktree root:
//   node web/tests/frame-metrics-probe.mjs
// Env:
//   WINE_DIST_DIR  installed dist to borrow runtime assets from (default:
//                  /home/ubuntu/boxedwine64/dist)
import { chromium } from '@playwright/test';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { mkdir, writeFile, readFile, readdir, symlink, rm, rename, stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serve } from '../../tools/browser/serve.mjs';

const REPO = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const DIST = process.env.WINE_DIST_DIR || '/home/ubuntu/boxedwine64/dist';
const TAG = process.env.TAG || `frame-metrics-${Date.now()}`;
if (!/^[\w.-]+$/.test(TAG)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const OUT = join(REPO, 'test-results', TAG);
const EXPORT = `/tmp/${TAG}-export`;
const CHROME = process.env.CHROME_PATH || '/usr/local/bin/chromium';
const BOOT_TIMEOUT = Number(process.env.BOOT_TIMEOUT || 900000);

const results = { tag: TAG, phases: {}, snapshots: {}, ok: true };
const record = (phase, name, pass, detail) => {
  results.phases[phase] ||= [];
  results.phases[phase].push({ name, pass, detail });
  if (!pass) results.ok = false;
  console.log(`  ${pass ? 'PASS' : 'FAIL'} ${phase}: ${name}${detail ? ' — ' + detail : ''}`);
};

// ---------------------------------------------------------------------------
// Candidate export: installed dist + this worktree's web tree on top.
// ---------------------------------------------------------------------------
async function assembleExport() {
  await rm(EXPORT, { recursive: true, force: true });
  await mkdir(EXPORT, { recursive: true });
  for (const entry of await readdir(DIST)) {
    await symlink(join(DIST, entry), join(EXPORT, entry));
  }
  const overlay = ['app.mjs', 'frame-metrics.mjs', 'vkwebgpu.mjs', 'vk-protocol.mjs', 'wgsl-utils.mjs',
    'shader.mjs', 'vk-log.mjs', 'index.html', 'style.css', 'launch.mjs', 'demos.mjs'];
  for (const file of overlay) {
    const target = join(EXPORT, file);
    await rm(target, { force: true });
    await symlink(join(REPO, 'web', file), target);
  }
  await rm(join(EXPORT, 'assets'), { recursive: true, force: true });
  await symlink(join(REPO, 'web/assets'), join(EXPORT, 'assets'));
  // The installed runtime assets (wasm/js/zips) stay the symlinks into DIST.
  const wasm = join(EXPORT, 'runtime', 'boxedwine64.wasm');
  const info = await stat(wasm);
  const sha = createHash('sha256').update(await readFile(wasm)).digest('hex');
  results.installedWasm = { bytes: info.size, sha256: sha };
  results.candidateOverlay = overlay;
  console.log(`EXPORT ${EXPORT} (installed wasm ${sha.slice(0, 12)}…)`);
}

// ---------------------------------------------------------------------------
// PHASE 1 — page tier on the real D3D9/DXVK path.
// ---------------------------------------------------------------------------
async function phasePageTier(browser, base) {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [], errors = [];
  page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => errors.push(String(e?.message || e)));

  await page.goto(`${base}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0`, { timeout: 120000 });
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: BOOT_TIMEOUT });
  console.log('  runtime ready');
  for (const [name, rel] of [['tri9pong.exe', 'tools/dxvk/tri9/tri9pong.exe'],
    ['d3d9.dll', 'tools/dxvk/build.w64/src/d3d9/d3d9.dll'],
    ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1']]) {
    const b64 = (await readFile(join('/home/ubuntu/boxedwine64', rel))).toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const ok = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
      if (ok !== 1) throw new Error('guest VFS registration failed for ' + name);
    }, { name, b64 });
  }
  // Let the tier be the page's own (top-level self-host) before the guest boots.
  await page.evaluate(async () => {
    const { installVkPageTier } = await import('../vkwebgpu.mjs');
    window.bwVkTier = installVkPageTier();
  });

  const until = Date.now() + BOOT_TIMEOUT;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > until) throw new Error('wine bootstrap sentinel timed out');
    await page.waitForTimeout(500);
  }
  console.log('  bootstrap complete');

  await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\home\\username\\tri9pong.exe',
      'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username']));

  const metrics = async () => page.evaluate(() => {
    try { return window.bwVkTier?.metrics?.() || null; } catch (e) { return { error: String(e?.message || e) }; }
  });
  let snapshot = null;
  const frameUntil = Date.now() + BOOT_TIMEOUT;
  for (;;) {
    snapshot = await metrics();
    if (snapshot?.v2 && snapshot.v2.presents >= 20) break;
    if (Date.now() > frameUntil) break;
    await page.waitForTimeout(1000);
  }
  results.snapshots.pageTier = snapshot;
  if (!snapshot?.v2) {
    record('pageTier', 'page tier reports metrics', false, JSON.stringify(snapshot)?.slice(0, 200) || 'null');
    await page.close();
    return;
  }

  const m = snapshot.v2;
  record('pageTier', 'present pacing is measured', m.presents >= 20 && m.fps > 0 && m.frameMs > 0,
    `${m.presents} presents, ${m.fps?.toFixed?.(1)} fps, ${m.frameMs?.toFixed?.(2)} ms/frame`);
  record('pageTier', 'submit/render spans are measured', m.submit?.count > 0 && m.render?.count > 0 && m.render.ms >= m.submit.ms,
    `submit ${m.submit.ms.toFixed(1)} ms over ${m.submit.count}, render ${m.render.ms.toFixed(1)} ms over ${m.render.count}`);
  record('pageTier', 'translate and pipeline counters exist', typeof m.translate?.ms === 'number' && typeof m.pipeline?.ms === 'number',
    `translate ${m.translate.ms.toFixed(1)} ms/${m.translate.count} events, pipeline ${m.pipeline.ms.toFixed(1)} ms/${m.pipeline.count}`);
  record('pageTier', 'translate is reported per event, not per frame',
    m.translate.count <= m.render.count, `${m.translate.count} translations for ${m.render.count} frames`);
  const gpu = m.gpu || {};
  const gpuOk = gpu.status === 'measured'
    ? (gpu.ms > 0 && gpu.samples > 0)
    : (gpu.status === 'inactive' || gpu.status === 'unavailable') && typeof gpu.reason === 'string' && gpu.reason.length > 0;
  record('pageTier', 'GPU time comes from timestamps or is explicitly unavailable', gpuOk,
    `${gpu.status}: ${gpu.status === 'measured' ? gpu.ms.toFixed(3) + ' ms over ' + gpu.samples + ' samples' : gpu.reason}`);
  record('pageTier', 'no page errors during the vk run', errors.length === 0, errors.slice(0, 3).join(' | '));
  await writeFile(join(OUT, 'phase1-metrics.json'), JSON.stringify({ snapshot, gpu, errors, logs: logs.slice(-40) }, null, 2));
  await page.screenshot({ path: join(OUT, 'phase1-vk-tier.png') });
  await page.close();
}

// ---------------------------------------------------------------------------
// PHASE 2 — the app shell's live panel in a real browser.
// ---------------------------------------------------------------------------
async function fixtureLine() {
  return (await readFile(join(REPO, 'web/tests/fixtures/frame-metrics-snapshot.txt'), 'utf8')).trim();
}

async function phasePanel(browser, base) {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const errors = [];
  page.on('pageerror', e => errors.push(String(e?.message || e)));
  await page.goto(`${base}/index.html`, { timeout: 120000 });
  await page.waitForFunction(() => !!document.querySelector('#gameCatalog [data-game]'), null, { timeout: 60000 });

  const statsButton = page.locator('#stats');
  record('panel', 'stats button starts unpressed', (await statsButton.getAttribute('aria-pressed')) === 'false');
  await page.locator('#gameCatalog [data-game="cube"]').click();
  // The player view (and the panel) mount as soon as a launch starts.
  await page.waitForFunction(() => document.getElementById('player')?.hidden === false, null, { timeout: 120000 });
  const hostVisible = await page.evaluate(() => document.getElementById('metricsHost')?.hidden === false);
  record('panel', 'panel mounts visible with the player', hostVisible);
  const hasRows = await page.locator('#metricsHost .metrics-row').count();
  record('panel', 'panel renders stage rows before any snapshot', hasRows >= 15, hasRows + ' rows');
  const unavailableRow = await page.locator('#metricsHost .metrics-row[data-state="unavailable"]').count();
  record('panel', 'unmeasured stages are marked unavailable', unavailableRow >= 1, unavailableRow + ' unavailable rows');
  record('panel', 'panel says the runtime snapshot is missing',
    (await page.locator('#metricsHost .metrics-note').allTextContents()).some(t => t.includes('bw64: METRICS')));

  // The Stats toggle drives both the host element and aria-pressed.
  await statsButton.click();
  await page.waitForTimeout(300);
  record('panel', 'stats button hides the panel',
    (await statsButton.getAttribute('aria-pressed')) === 'false'
    && await page.evaluate(() => document.getElementById('metricsHost')?.hidden === true));
  await statsButton.click();
  await page.waitForTimeout(300);
  record('panel', 'stats button shows the panel again',
    (await statsButton.getAttribute('aria-pressed')) === 'true'
    && await page.evaluate(() => document.getElementById('metricsHost')?.hidden === false));

  // Feed the REAL writer output through the guest's own log channel: app.mjs
  // must recognise it, keep it out of the log pane and put it in the panel.
  const line = await fixtureLine();
  const frame = page.frames().find(f => f.url().includes('runtime/index.html'));
  if (!frame) throw new Error('runtime frame not found');
  await frame.evaluate((text) => {
    window.parent.postMessage({ wine: true, type: 'log', data: 'vkboot\n' + text }, location.origin);
  }, line);
  await page.waitForTimeout(1200);
  const rows = await page.evaluate(() => Object.fromEntries([...document.querySelectorAll('#metricsHost .metrics-row')]
    .map(r => [r.querySelector('.metrics-label')?.textContent || '',
      { value: r.querySelector('.metrics-number')?.textContent || '', state: r.dataset.state || '' }])));
  const logText = await page.locator('#log').textContent();
  record('panel', 'runtime snapshot reaches the panel', rows['Vulkan submit']?.value === '0.90 ms',
    'Vulkan submit = ' + (rows['Vulkan submit']?.value ?? 'missing'));
  record('panel', 'JIT rows reflect the snapshot', String(rows['x86-64 guest']?.value || '').startsWith('running'),
    String(rows['x86-64 guest']?.value));
  record('panel', 'the snapshot line stays out of the log pane', !logText.includes('bw64: METRICS'),
    logText.includes('bw64: METRICS') ? 'line leaked into the log' : 'suppressed');
  record('panel', 'guest layers stay unavailable', rows['dxvk']?.state === 'unavailable',
    'dxvk = ' + rows['dxvk']?.state);
  record('panel', 'GPU row is not faked from submit time',
    rows['GPU execution']?.value === '--' || Number.parseFloat(rows['GPU execution']?.value) > 0,
    'GPU execution = ' + rows['GPU execution']?.value);

  // Responsiveness: 200 panel ticks must stay far below a frame budget each.
  // Aggregation cost on the real snapshot: 200 view() passes must be far under
  // one frame each (the panel refreshes at ~1 Hz).
  const tick = await page.evaluate(async (text) => {
    const module = await import('./frame-metrics.mjs');
    const snapshot = module.parseMetricsLine(text);
    if (!snapshot) throw new Error('fixture line did not parse in the browser');
    const model = module.createMetricsModel();
    model.setRunState('running');
    model.applyWasm(snapshot, 0);
    let t0 = performance.now();
    for (let i = 0; i < 200; i++) model.view(t0 + i);
    const totalMs = performance.now() - t0;
    return { totalMs, perTick: totalMs / 200, rows: model.view(t0).groups.reduce((n, g) => n + g.rows.length, 0) };
  }, line).catch(error => ({ error: String(error?.message || error) }));
  if (tick && typeof tick.perTick === 'number') {
    record('panel', 'aggregation stays cheap on the real snapshot', tick.perTick < 2,
      tick.perTick.toFixed(3) + ' ms per view() over ' + tick.rows + ' rows');
  } else {
    record('panel', 'aggregation stays cheap on the real snapshot', false, JSON.stringify(tick));
  }

  await page.screenshot({ path: join(OUT, 'phase2-panel.png') });
  await page.locator('#metricsHost').screenshot({ path: join(OUT, 'phase2-panel-crop.png') }).catch(() => {});
  await writeFile(join(OUT, 'phase2-rows.json'), JSON.stringify({ rows, errors }, null, 2));
  record('panel', 'no page errors in the app shell', errors.length === 0, errors.slice(0, 3).join(' | '));
  await page.close();
}

// ---------------------------------------------------------------------------
await mkdir(OUT, { recursive: true });
await waitForBuildIdle();
const slot = await acquireProbeSlot();
let server = null, browser = null;
try {
  await assembleExport();
  server = await serve(EXPORT);
  const base = `http://127.0.0.1:${server.address().port}`;
  browser = await chromium.launch({
    executablePath: CHROME, headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
      '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
      '--enable-features=Vulkan'],
  });
  results.chromium = execFileSync(CHROME, ['--version']).toString().trim();
  console.log('PHASE 1: page tier (D3D9/DXVK)');
  await phasePageTier(browser, base);
  console.log('PHASE 2: app shell panel');
  await phasePanel(browser, base);
} catch (error) {
  results.ok = false;
  results.fatal = String(error?.stack || error);
  console.error('PROBE ERROR', error);
} finally {
  try { await browser?.close(); } catch {}
  try { await new Promise(ok => server?.close(ok)); } catch {}
  await slot.release();
  await writeFile(join(OUT, 'summary.json'), JSON.stringify(results, null, 2));
  console.log(`\nSUMMARY ${results.ok ? 'PASS' : 'FAIL'} -> ${join(OUT, 'summary.json')}`);
  const failed = Object.entries(results.phases).flatMap(([phase, list]) =>
    list.filter(r => !r.pass).map(r => `${phase}: ${r.name} (${r.detail || ''})`));
  if (failed.length) console.log('FAILED:\n  ' + failed.join('\n  '));
  process.exitCode = results.ok ? 0 : 1;
}