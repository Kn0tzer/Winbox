import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';

const tag = 'vkboot64-verdict1';
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

  // 64-bit boot: wine cmd /c echo vkboot (default arch, prefix64.zip)
  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&';
  await page.goto(url, { timeout: 60000 });
  console.log('NAVIGATED', url);

  try {
    await page.waitForFunction(() => {
      return document.title.includes('done') || window.__vkbootDone === true;
    }, null, { timeout: 300000 });
    console.log('DONE-WAIT');
  } catch (e) {
    console.log('TIMEOUT waiting');
  }

  await page.waitForTimeout(15000);

  const logText = logs.join('\n');
  await writeFile(dir + '/console.log', logText);

  // Verdict metrics
  const vkbootRaw = (logText.match(/^log: vkboot$/gm) || []).length;
  const vkbootPrefixed = (logText.match(/^\s*log: \[guest fd=\d+[^\]]*\] vkboot\s*$/gm) || []).length;
  const setupapiAbort = (logText.match(/unimplemented function, aborting/gi) || []).length;
  const setupapiMentions = (logText.match(/SetupDiCreateDeviceInfoList/g) || []).length;
  const winebootExit1 = (logText.match(/wineboot[^]*?exit status 1|EXIT.*1/gi) || []).length;
  // wineboot exit status lines look like: "wineboot.exe ... exit status 1" or pid exit lines
  const winebootExits = [...logText.matchAll(/wineboot[^\n]*exit[^\n]*(\d+)/gi)].map(m => m[1]);
  const c0000020 = (logText.match(/c0000020/gi) || []).length;
  const updateTimestamp = (logText.match(/update-timestamp/gi) || []).length;

  const summary = {
    tag,
    date_utc: new Date().toISOString(),
    vkbootRaw,
    vkbootPrefixed,
    vkbootFound: (vkbootRaw + vkbootPrefixed) > 0,
    setupapiAbort,
    setupapiMentions,
    winebootExits,
    c0000020,
    updateTimestamp,
    logLines: logs.length,
    passed: (vkbootRaw + vkbootPrefixed) > 0
  };
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary));

  await browser.close();
  server.close();
} finally {
  await slot.release();
}
console.log('DONE');
