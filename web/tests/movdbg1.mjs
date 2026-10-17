import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = 'movdbg1';
const dir = '/home/ubuntu/boxedwine64/test-results/' + tag;
const { mkdir, writeFile } = await import('node:fs/promises');
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
try {
  const server = await serve('/home/ubuntu/boxedwine64/dist');
  const port = server.address().port;
  const browser = await chromium.launch({
    executablePath: '/usr/local/bin/chromium',
    headless: true,
    args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan']
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [];
  page.on('console', m => logs.push(m.type() + ': ' + m.text()));
  page.on('pageerror', e => logs.push('pageerror: ' + e.message));
  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&';
  await page.goto(url, { timeout: 60000 });
  try {
    await page.waitForFunction(() => document.title.includes('done') || window.__vkbootDone === true, null, { timeout: 120000 });
  } catch (e) { console.log('TIMEOUT'); }
  try { await page.waitForTimeout(10000); } catch(e) {}
  const logText = logs.join('\n');
  await writeFile(dir + '/console.log', logText);
  const movdbg = (logText.match(/MOVDBG.*/g) || []);
  await writeFile(dir + '/movdbg.log', movdbg.join('\n'));
  console.log('MOVDBG count:', movdbg.length);
  movdbg.slice(0, 10).forEach(l => console.log(l));
  await browser.close();
  server.close();
} finally {
  await slot.release();
}
console.log('DONE');
