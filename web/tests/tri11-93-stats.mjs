// D3D11 FL 9_3 probe: device creation + offscreen triangle + readback.
// Run from repo root: TAG=unique TIMEOUT=1800000 node /tmp/tri11-93-probe.mjs
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const slot = await acquireProbeSlot();
try {
const tag = process.env.TAG || `tri11-93-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/dxvk-${tag}`;
await mkdir(dir, { recursive: true });
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
page.on('pageerror', e => errors.push(e.message));
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const timeout = Number(process.env.TIMEOUT || 1200000);
let summary = { passed: false };
try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=0&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  const assets = [['tri11_93.exe', process.env.TRI11_PATH || 'tools/d3dtests/tri11_93.exe'],
    ['d3d11.dll', process.env.D3D11_PATH || 'tools/dxvk/build.w64/src/d3d11/d3d11.dll'],
    ['dxgi.dll', process.env.DXGI_PATH || 'tools/dxvk/build.w64/src/dxgi/dxgi.dll'],
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

  const bootstrapUntil = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) throw new Error('Wine bootstrap sentinel timed out');
    await flush();
    await page.waitForTimeout(500);
  }
  console.log('BOOTSTRAP COMPLETE');

  await page.evaluate(async () => {
    const { installVkPageTier } = await import('../vkwebgpu.mjs');
    window.bwVkTier = installVkPageTier();
    window.bwVkTierReady = Promise.resolve(window.bwVkTier);
  });

  const spawn = await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\home\\username\\tri11_93.exe', 'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d11=n;dxgi=n\nLD_LIBRARY_PATH=/home/username']));
  console.log('SPAWN', spawn);

  const until = Date.now() + timeout;
  while (Date.now() < until) {
    await flush();
    const resultLine = logs.find(l => /tri1193: RESULT [01]/.test(l));
    if (resultLine) {
      console.log('RESULT LINE:', resultLine);
      const flLine = logs.find(l => /tri1193: D3D11CreateDevice OK/.test(l));
      const litLine = logs.find(l => /tri1193: LIT=/.test(l));
      const failLine = logs.find(l => /tri1193: FAIL/.test(l));
      console.log('FL:', flLine || 'none', '| LIT:', litLine || 'none', '| FAIL:', failLine || 'none');
      summary = { passed: /RESULT 0/.test(resultLine), fl: flLine || null, lit: litLine || null, fail: failLine || null };
      try {
        const st = await page.evaluate(() => window.bwVkStats ? window.bwVkStats() : null);
        console.log('PAGESTATS: ' + JSON.stringify(st, (k,v) => (k==='pipeline'||k==='pipeline2') ? '[obj]' : v));
      } catch(e) { console.log('PAGESTATS failed: ' + e.message); }
      break;
    }
    await page.waitForTimeout(2000);
  }
  console.log('SUMMARY', JSON.stringify(summary));
} catch (e) {
  console.log('PROBE ERROR', e.message);
  summary.error = e.message;
} finally {
  await flush();
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await browser.close();
  server.close();
}
console.log('DONE slot=' + slot.index);
} finally {
  await slot.release();
}
