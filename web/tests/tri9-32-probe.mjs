// 32-bit D3D9 smoke test: d3dconf9-32.exe via DXVK -> vk32Bridge.
// Stages 32-bit d3d9.dll + libvulkan.so.1, runs with ?arch=x86.
// Goal: determine if 32-bit D3D9 renders (or where it fails).
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';

const tag = 'tri9-32-smoke1';
const dir = '/home/ubuntu/boxedwine64/test-results/' + tag;
const { mkdir, writeFile, readFile } = await import('node:fs/promises');
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
try {
  await waitForBuildIdle();
  const server = await serve('/home/ubuntu/boxedwine64/dist');
  const port = server.address().port;
  console.log('SERVER', port);

  const browser = await chromium.launch({
    executablePath: '/usr/local/bin/chromium',
    headless: true,
    args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan']
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [];
  page.on('console', m => logs.push(m.type() + ': ' + m.text()));
  page.on('pageerror', e => logs.push('pageerror: ' + e.message));

  // 32-bit: ?arch=x86
  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?arch=x86&session=1&persist=0&';
  await page.goto(url, { timeout: 60000 });
  console.log('NAVIGATED');

  // Wait for runtime to be ready
  await page.waitForFunction(() => window.bwRuntime && window.bwRuntime.fs, null, { timeout: 120000 });
  console.log('RUNTIME READY');

  // Stage 32-bit assets
  const assets = [
    ['d3dconf9-32.exe', '/home/ubuntu/boxedwine64/tools/d3dtests/d3dconf9-32.exe'],
    ['d3d9.dll', '/home/ubuntu/boxedwine64/tools/dxvk/build.w32/src/d3d9/d3d9.dll'],
    ['libvulkan.so.1', '/home/ubuntu/boxedwine64/tools/rootfs64/libvk32/libvulkan.so.1'],
  ];
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      try {
        window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
      } catch (e) { console.log('register_file note:', e.message); }
    }, { name, b64 });
    console.log('STAGED', name, bytes.length);
  }

  // Spawn 32-bit tri9 with DXVK d3d9
  const env = 'BW64_VKTRACE=2\nLD_LIBRARY_PATH=/home/username\nWINEDEBUG=+loaddll';
  const spawn = await page.evaluate(([env]) => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine\nZ:\\\\home\\\\username\\\\d3dconf9-32.exe', env]), [env]);
  console.log('SPAWNED', spawn);

  // Wait and collect logs
  await page.waitForTimeout(120000);
  const logText = logs.join('\n');
  await writeFile(dir + '/console.log', logText);

  // Analyze
  const vk32Traps = (logText.match(/vk32:/g) || []).length;
  const vk32First = logText.includes('vk32: FIRST trap');
  const d3d9Loaded = logText.includes('d3d9.dll') || logText.includes('D3D9');
  const renders = (logText.match(/vkCmdDraw|vkQueueSubmit/g) || []).length;
  const d3dOps = (logText.match(/d3dconf9: OP/g) || []).length;
  const d3dDone = logText.includes('d3dconf9: DONE');
  const errors = logText.split('\n').filter(l => l.toLowerCase().includes('error') || l.includes('pageerror')).slice(0, 20);

  const summary = { tag, vk32Traps, vk32First, d3d9Loaded, renders, d3dOps, d3dDone, errors };
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary, null, 2));

  await browser.close();
  server.close();
} finally {
  await slot.release();
}
