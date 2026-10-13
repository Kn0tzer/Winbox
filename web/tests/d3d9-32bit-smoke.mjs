// 32-bit D3D9 smoke test: d3dconf9-32.exe via DXVK -> vk32Bridge.
// PREPARED — DO NOT RUN until the 32-bit Wine workaround passes 5-boot verification.
// Stages 32-bit DXVK d3d9.dll + 32-bit libvulkan.so.1, runs with ?arch=x86.
// Pass bar: bwVkStats().litPixels > 100 (a 32-bit D3D9 render reached the page).
// Run from repo root: TAG=unique node web/tests/d3d9-32bit-smoke.mjs
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';

const tag = process.env.TAG || `d3d9-32bit-smoke-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/dxvk-${tag}`;
await mkdir(dir, { recursive: true });

// ---- Coordination: one probe at a time (probe slot + build idle) ----
await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);

let summary = { pass: false, tag };
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const port = server.address().port;
console.log('SERVER', port);
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
  headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
    '--enable-features=Vulkan'],
});
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [], errors = [];
  page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => errors.push(e.message));
  const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));

  // 32-bit mode: ?arch=x86 selects wine32.zip + prefix32.zip + /usr/lib/wine/wine.
  await page.goto(`http://127.0.0.1:${port}/runtime/index.html?arch=x86&session=1&persist=0`, { timeout: 60000 });
  console.log('NAVIGATED');
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 1800000 });
  console.log('RUNTIME READY');

  // Stage 32-bit D3D9 assets (mirrors tri9-32-probe.mjs).
  const assets = [
    ['d3dconf9-32.exe', 'tools/d3dtests/d3dconf9-32.exe'],
    ['d3d9.dll', 'tools/dxvk/build.w32/src/d3d9/d3d9.dll'],
    ['libvulkan.so.1', 'tools/rootfs64/libvk32/libvulkan.so.1'],
  ];
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
    console.log('STAGED', name, bytes.length);
  }

  // Spawn via the 32-bit Wine loader; force DXVK's d3d9 over Wine's built-in.
  const spawn = await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine\nZ:\\\\home\\\\username\\\\d3dconf9-32.exe',
     'BW64_VKTRACE=0\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username']));
  console.log('SPAWN', spawn);
  const drainUntil = Date.now() + 60000;
  while (Date.now() < drainUntil && !logs.some(l => /spawning.*d3dconf9-32/i.test(l))) await page.waitForTimeout(1000);
  console.log('SPAWN-DRAIN', logs.some(l => /spawning.*d3dconf9-32/i.test(l)) ? 'YES' : 'NO');

  // Poll for the first rendered frame: bwVkStats().litPixels > 100.
  const getStats = () => page.evaluate(() => {
    try { const s = window.bwVkStats?.(); return s ? { lit: s.litPixels || 0, rendered: s.v2?.rendered || 0 } : null; }
    catch (e) { return null; }
  }).catch(() => null);

  let lit = 0, rendered = 0;
  const litUntil = Date.now() + 600000; // 10 min: 32-bit first frame may be slow
  while (Date.now() < litUntil && lit <= 100) {
    const st = await getStats();
    if (st) { if (st.lit > 0) lit = st.lit; rendered = st.rendered; }
    await flush();
    await page.waitForTimeout(2000);
  }
  console.log('LITPIXELS', lit, 'RENDERED', rendered);

  // Screenshot for manual inspection (SwiftShader may not composite; litPixels is the verdict).
  try { await page.screenshot({ path: `${dir}/render.png` }); console.log('SCREENSHOT render.png'); }
  catch (e) { console.log('SCREENSHOT FAILED:', String(e.message || e).slice(0, 120)); }

  await flush();
  const joined = logs.join('\n');
  const d3dOps = (joined.match(/d3dconf9: OP/g) || []).length;
  const d3dDone = /d3dconf9: DONE/.test(joined);
  const vk32Traps = (joined.match(/vk32:/g) || []).length;
  const pass = lit > 100;
  summary = {
    pass, tag,
    render: { litPixels: lit, rendered, d3dOps, d3dDone, vk32Traps },
    errors: errors.slice(0, 20),
  };
  console.log('SMOKE VERDICT', pass ? 'PASS' : 'FAIL');
} catch (e) {
  summary.failure = String(e.stack || e);
  console.log('SMOKE ERROR', summary.failure.slice(0, 300));
} finally {
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('SUMMARY WRITTEN', `${dir}/summary.json`);
  try { await Promise.race([browser.close(), new Promise((_, rej) => setTimeout(() => rej(new Error('close-timeout')), 15000))]); }
  catch (e) { console.log('BROWSER CLOSE ISSUE:', String(e && e.message || e).slice(0, 120)); }
  server.close();
  await slot.release();
  if (!summary.pass) process.exitCode = 1;
}
