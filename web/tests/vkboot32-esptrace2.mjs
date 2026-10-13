import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';

const tag = 'vkboot32-esptrace2';
const dir = '/home/ubuntu/boxedwine64/test-results/' + tag;
const { mkdir, writeFile } = await import('node:fs/promises');
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

  // 32-bit boot: wine cmd /c echo vkboot (with prefix32.zip)
  // URL pattern from vkboot32-lea5: arch=x86, prefix, and cmd
  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&';
  await page.goto(url, { timeout: 60000 });
  console.log('NAVIGATED', url);

  // Wait for the vkboot output or timeout (5 min)
  try {
    await page.waitForFunction(() => {
      // Check if we've seen the vkboot output in logs (via console)
      return document.title.includes('done') || window.__vkbootDone === true;
    }, null, { timeout: 300000 });
    console.log('DONE-WAIT');
  } catch (e) {
    console.log('TIMEOUT waiting');
  }

  // Wait a bit more for logs to flush
  await page.waitForTimeout(15000);

  const logText = logs.join('\n');
  await writeFile(dir + '/console.log', logText);

  // Analyze
  const vkbootCount = (logText.match(/^log: vkboot$/gm) || []).length;
  const crashD3F8 = (logText.match(/ntdll\.so\+0xD3F8|\b0xD3F8\b/g) || []).length;
  const esiZero = (logText.match(/ESI=00000000/g) || []).length;
  const espTraceCount = (logText.match(/ESPTRACE:/g) || []).length;
  const wineserver32 = (logText.match(/wineserver32|server32/gi) || []).length;
  const kernel32Missing = (logText.match(/could not load kernel32.dll/g) || []).length;

  const summary = {
    tag,
    vkbootFound: vkbootCount > 0,
    vkbootCount,
    crashD3F8,
    esiZero,
    espTraceCount,
    wineserver32,
    kernel32Missing,
    logLines: logs.length,
    passed: vkbootCount > 0
  };
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary));

  await browser.close();
  server.close();
} finally {
  await slot.release();
}
console.log('DONE');
