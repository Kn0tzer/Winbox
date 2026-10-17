import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = 'fix-test1';
const dir = '/home/ubuntu/boxedwine64/test-results/' + tag;
const { mkdir, writeFile } = await import('node:fs/promises');
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
try {
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

  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&';
  await page.goto(url, { timeout: 60000 });
  console.log('NAVIGATED');

  try {
    await page.waitForFunction(() => {
      return document.title.includes('done') || window.__vkbootDone === true;
    }, null, { timeout: 180000 });
    console.log('DONE-WAIT');
  } catch (e) {
    console.log('TIMEOUT waiting');
  }

  await page.waitForTimeout(10000);
  const logText = logs.join('\n');
  await writeFile(dir + '/console.log', logText);

  // Check for the control-flow corruption pattern
  const asciiEip = (logText.match(/EIP=3078303D/g) || []).length;
  const segMapper = (logText.match(/SEG_MAPPER/g) || []).length;
  const vkboot = (logText.match(/vkboot/g) || []).length;

  console.log('ASCII_EIP(3078303D):', asciiEip);
  console.log('SEG_MAPPER:', segMapper);
  console.log('vkboot mentions:', vkboot);

  await browser.close();
  server.close();
} finally {
  await slot.release();
}
console.log('PROBE COMPLETE');
