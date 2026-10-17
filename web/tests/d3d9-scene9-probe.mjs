// D3D9 scene9 probe: complex D3D9 scene (28 draws/frame, textures, alpha blend) via DXVK.
// Rendered-frames + frame-time measurement. scene9.exe handles no gameplay input
// (WM_CLOSE only), so this probe validates rendering + perf; input is covered by
// the fpsinput probe.
// Run from repo root: TAG=unique TIMEOUT=1800000 node web/tests/d3d9-scene9-probe.mjs
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { acquireProbeSlot } from './probe-slots.mjs';

const tag = process.env.TAG || `scene9-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/dxvk-${tag}`;
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
let summary = { gameSuccess: false };
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [], errors = [];
  page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => errors.push(e.message));
  const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
  const timeout = Number(process.env.TIMEOUT || 1800000);

  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  const assets = [['scene9.exe', process.env.SCENE9_PATH || 'tools/d3dtests/scene9.exe'],
    ['d3d9.dll', process.env.D3D9_PATH || 'tools/dxvk/build.w64/src/d3d9/d3d9.dll'],
    ['libvulkan.so.1', process.env.VK_SHIM_PATH || 'tools/rootfs64/libvk64/libvulkan.so.1']];
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const registered = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
      if (registered !== 1) throw new Error('Guest VFS registration failed for ' + name);
    }, { name, b64 });
  }
  console.log('ASSETS UPLOADED');

  const bootstrapUntil = Date.now() + 90000;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) { console.log('BOOTSTRAP SENTINEL SKIPPED (timeout)'); break; }
    await flush();
    await page.waitForTimeout(500);
  }
  console.log('BOOTSTRAP COMPLETE');

  await page.evaluate(async () => {
    const { installVkPageTier } = await import('../vkwebgpu.mjs');
    window.bwVkTier = installVkPageTier();
  });
  console.log('TIER INSTALLED');

  const spawn = await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\scene9.exe', 'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username']));
  console.log('SPAWN', spawn);
  const drainUntil = Date.now() + 60000;
  while (Date.now() < drainUntil && !logs.some(l => /spawning.*scene9/.test(l))) await page.waitForTimeout(1000);
  console.log('SPAWN-DRAIN', logs.some(l => /spawning.*scene9/.test(l)) ? 'YES' : 'NO');

  const getStats = () => page.evaluate(() => {
    try { const s = window.bwVkStats?.(); return s ? { lit: s.litPixels || 0, rendered: s.v2?.rendered || 0 } : null; }
    catch (e) { return null; }
  }).catch(() => null);

  // Wait for the scene to render.
  const until = Date.now() + timeout;
  let lit = 0;
  while (Date.now() < until) {
    const st = await getStats();
    if (st === null) throw new Error('page died during render wait');
    if (st.lit > 0) lit = st.lit;
    if (lit > 1000) break;
    if (logs.some(l => /\bscene9: RESULT [01]\b/.test(l))) throw new Error('scene9 exited before rendering');
    await page.waitForTimeout(1000);
  }
  if (lit <= 1000) throw new Error('no scene rendered (litPixels=' + lit + ')');
  console.log('LITPIXELS', lit);

  // Frame-time measurement: rendered-counter deltas over a timed window.
  const ft0 = await getStats(); const t0 = Date.now();
  await page.waitForTimeout(30000);
  const ft1 = await getStats(); const t1 = Date.now();
  const framesAdvanced = ft1.rendered - ft0.rendered;
  const meanFrameMs = framesAdvanced > 0 ? (t1 - t0) / framesAdvanced : -1;
  console.log('FRAMETIME', JSON.stringify({ framesAdvanced, meanFrameMs }));

  // Wait for completion (120 frames).
  while (Date.now() < until) {
    if (logs.some(l => /\bscene9: RESULT [01]\b/.test(l))) break;
    await page.waitForTimeout(2000);
    await flush();
  }
  const joined = logs.join('\n');
  const result = /\bscene9: RESULT 0\b/.test(joined) ? 0 : /\bscene9: RESULT 1\b/.test(joined) ? 1 : null;
  const frameLogs = logs.filter(l => /scene9: frame \d+ draws=\d+/.test(l));
  const drawsOk = frameLogs.length > 0 && frameLogs.every(l => /draws=28/.test(l));
  const final = await getStats();
  console.log('RESULT', result, 'frameLogs', frameLogs.length, 'drawsOk', drawsOk);

  summary = {
    gameSuccess: !!(result === 0 && lit > 1000 && drawsOk),
    rendered: { litPixels: lit, finalLit: final?.lit, framesAdvancedTotal: final?.rendered, result },
    frameTime: { windowMs: t1 - t0, framesAdvanced, meanFrameMs },
    frameLogs: frameLogs.length, drawsOk,
    errors,
  };
} catch (e) {
  summary.failure = String(e.stack || e);
} finally {
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('GAME VERDICT', JSON.stringify(summary.gameSuccess));
  await browser.close(); server.close(); await slot.release();
  if (!summary.gameSuccess) process.exitCode = 1;
}
