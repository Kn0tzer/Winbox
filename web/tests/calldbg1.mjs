import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';

const tag = 'calldbg1';
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

  // 32-bit boot to trigger ld.so CALLs
  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&';
  await page.goto(url, { timeout: 60000 });
  console.log('NAVIGATED', url);

  // Wait for boot or timeout (3 min - we just need the ld.so CALLs)
  try {
    await page.waitForFunction(() => {
      return document.title.includes('done') || window.__vkbootDone === true;
    }, null, { timeout: 180000 });
    console.log('DONE-WAIT');
  } catch (e) {
    console.log('TIMEOUT waiting (expected - just need CALLDBG logs)');
  }

  await page.waitForTimeout(10000);

  const logText = logs.join('\n');
  await writeFile(dir + '/console.log', logText);

  // Analyze CALLDBG output
  const calldbgLines = logText.split('\n').filter(l => l.includes('CALLDBG'));
  const calldbg2Lines = logText.split('\n').filter(l => l.includes('CALLDBG2'));

  await writeFile(dir + '/calldbg.log', calldbgLines.join('\n'));
  await writeFile(dir + '/calldbg2.log', calldbg2Lines.join('\n'));

  const summary = {
    tag,
    calldbgCount: calldbgLines.length,
    calldbg2Count: calldbg2Lines.length,
    logLines: logs.length,
  };
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary));
  console.log('CALLDBG_SAMPLE', calldbgLines.slice(0, 10).join(' | '));

  await browser.close();
  server.close();
} finally {
  await slot.release();
}
console.log('DONE');
