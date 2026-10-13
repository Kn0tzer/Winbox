import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';

const tag = 'wine10-freshdiag1';
const dir = '/home/ubuntu/boxedwine64/test-results/' + tag;
const { mkdir, writeFile } = await import('node:fs/promises');
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
const logs = [];
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
  page.on('console', m => logs.push(m.type() + ': ' + m.text()));
  page.on('pageerror', e => logs.push('pageerror: ' + e.message));

  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&';
  try {
    await page.goto(url, { timeout: 60000 });
    console.log('NAVIGATED', url);
  } catch (e) { console.log('NAV-ERR', e.message); }

  try {
    await page.waitForFunction(() => {
      return document.title.includes('done') || window.__vkbootDone === true;
    }, null, { timeout: 240000 });
    console.log('DONE-WAIT');
  } catch (e) {
    console.log('TIMEOUT waiting');
  }

  try { await page.waitForTimeout(10000); } catch (e) { console.log('FLUSH-ERR', e.message); }

  await browser.close().catch(()=>{});
  server.close();
} catch (e) {
  console.log('PROBE-ERR', e.message);
} finally {
  const logText = logs.join('\n');
  await writeFile(dir + '/console.log', logText);
  const vkbootCount = (logText.match(/^log: vkboot$/gm) || []).length;
  const summary = {
    tag,
    vkbootFound: vkbootCount > 0,
    vkbootCount,
    logLines: logs.length,
    passed: vkbootCount > 0
  };
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary));
  console.log('LOGS-WRITTEN', logs.length);
  await slot.release();
}
console.log('DONE');
