// Minimal probe: stage sintest.exe, spawn, capture stdout, exit.
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';

const tag = process.env.TAG || 'sintest-1';
const dir = `test-results/${tag}`;
await mkdir(dir, { recursive: true });
await waitForBuildIdle();
const slot = await acquireProbeSlot();
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
  headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
    '--enable-features=Vulkan'],
});
try {
  const page = await browser.newPage();
  const logs = [];
  page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => logs.push('pageerror: ' + e.message));
  const timeout = 600000;
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  const bytes = await readFile('/tmp/audioprobe/sintest.exe');
  await page.evaluate(({ b64 }) => {
    const fs = window.bwRuntime.fs();
    fs.mkdirTree('/root/home/username');
    fs.writeFile('/root/home/username/sintest.exe', Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
    if (window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/sintest.exe']) !== 1)
      throw new Error('register failed');
  }, { b64: bytes.toString('base64') });
  console.log('STAGED');
  const t0 = Date.now();
  while (!logs.some(l => /^log: vkboot\s*$/.test(l)) && Date.now() - t0 < 120000)
    await page.waitForTimeout(500);
  await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\home\\username\\sintest.exe', 'LD_LIBRARY_PATH=/home/username']));
  const t1 = Date.now();
  let done = false;
  while (!done && Date.now() - t1 < 180000) {
    await page.waitForTimeout(1000);
    await writeFile(`${dir}/console.log`, logs.join('\n'));
    done = logs.some(l => /sintest: DONE/.test(l));
  }
  console.log('SINTEST LINES:');
  logs.filter(l => /sintest:/.test(l)).forEach(l => console.log('  ' + l));
  await writeFile(`${dir}/console.log`, logs.join('\n'));
} finally {
  await browser.close();
  server.close();
  await slot.release();
}
