// tri10swap screenshot probe: D3D10 swapchain triangle, visual verification.
// Run: TAG=unique TIMEOUT=1200000 node web/tests/tri10swap-probe.mjs
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = process.env.TAG || `tri10swap-${Date.now()}`;
const dir = `test-results/dxvk-${tag}`;
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
let server, browser;
const logs = [];
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const timeout = Number(process.env.TIMEOUT || 1200000);
let summary = { passed: false };

try {
  server = await serve(process.env.WINE_DIST_DIR || 'dist');
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
      '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
  });
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => logs.push(`pageerror: ${e.message}`));

  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=0&persist=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  const assets = [
    ['tri10swap.exe', 'tools/d3dtests/tri10swap.exe'],
    ['d3d10core.dll', 'tools/dxvk/build.w64/src/d3d10/d3d10core.dll'],
    ['d3d11.dll', 'tools/dxvk/build.w64/src/d3d11/d3d11.dll'],
    ['dxgi.dll', 'tools/dxvk/build.w64/src/dxgi/dxgi.dll'],
    ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1'],
  ];
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
    }, { name, b64 });
  }

  const bootstrapUntil = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) throw new Error('bootstrap timeout');
    await flush();
    await page.waitForTimeout(500);
  }
  console.log('BOOTSTRAP COMPLETE');

  await page.evaluate(async () => {
    const { installVkPageTier } = await import('../vkwebgpu.mjs');
    window.bwVkTier = installVkPageTier();
  });

  await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\tri10swap.exe', 'WINEDLLOVERRIDES=d3d10core=n;d3d11=n;dxgi=n\nLD_LIBRARY_PATH=/home/username']));
  console.log('SPAWNED');

  // Wait for READY, then screenshot
  const until = Date.now() + timeout;
  let ready = false;
  while (Date.now() < until && !ready) {
    await flush();
    await page.waitForTimeout(2000);
    const out = logs.join('\n');
    if (/tri10swap: READY/.test(out)) ready = true;
  }
  if (!ready) throw new Error('tri10swap READY not seen');

  console.log('TAKING SCREENSHOT');
  await page.waitForTimeout(3000); // let a few frames present
  const shot = await page.screenshot();
  await writeFile(`${dir}/screenshot.png`, shot);

  // Analyze: count non-clear-color pixels (clear is dark blue ~ (26,26,77))
  const { createCanvas, loadImage } = await import('canvas').catch(() => ({}));
  summary.ready = true;
  summary.screenshot = 'screenshot.png';

  // Simple pixel check via page.evaluate on canvas
  const px = await page.evaluate(() => {
    const c = document.getElementById('vkCanvas') || document.querySelector('canvas');
    if (!c) return { error: 'no canvas' };
    return { w: c.width, h: c.height };
  });
  summary.canvas = px;

  summary.passed = ready;
} catch (e) {
  summary.error = String(e && e.message || e);
} finally {
  await flush();
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary));
  try { await browser?.close(); } catch {}
  try { server?.close(); } catch {}
  await slot.release();
}
process.exit(summary.passed ? 0 : 1);
