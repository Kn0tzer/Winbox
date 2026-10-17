import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';

const tag = 'vkboot32-bug6fix1';
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

  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&';
  await page.goto(url, { timeout: 60000 });
  console.log('NAVIGATED');

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

  const bug4skip = (logText.match(/BUG4_SKIP/g) || []).length;
  const bug4Lines = logText.split('\n').filter(l => l.includes('BUG4_SKIP')).slice(0, 5);
  const inittable = (logText.match(/INITTABLE_SKIP/g) || []).length;
  const gotfix = (logText.match(/GOTFIX/g) || []).length;
  const findenv = (logText.match(/FINDENV_INTERCEPT/g) || []).length;
  const vkbootCount = (logText.match(/vkboot/gi) || []).length;
  const crashB5D71 = (logText.match(/0xB5D71|B5D71|xpg_strerror/i) || []).length;
  const crashLines = logText.split('\n').filter(l => /crash|fault|FATAL|SIGSEGV/i.test(l)).slice(0, 20);
  const bug5skip = (logText.match(/BUG5_SKIP/g) || []).length;
  const bug5Lines = logText.split('\n').filter(l => l.includes('BUG5_SKIP')).slice(0, 5);
  const segAccess = (logText.match(/SEG_ACCESS/g) || []).length;
  const segAccessLines = logText.split('\n').filter(l => l.includes('SEG_ACCESS')).slice(0, 5);
  const binpatch = (logText.match(/BINPATCH/g) || []).length;
  const binpatchLines = logText.split('\n').filter(l => l.includes('BINPATCH')).slice(0, 10);
  const nullskip = (logText.match(/NULLSKIP/g) || []).length;
  const nullskipLines = logText.split('\n').filter(l => l.includes('NULLSKIP')).slice(0, 10);
  const segMapperFBB4 = (logText.match(/SEG_MAPPER[^\n]*1064FBB4/gi) || []).length;
  const freeInvalid = (logText.match(/free\(\): invalid pointer/gi) || []).length;
  const pebwatch = (logText.match(/PEBWATCH/gi) || []).length;
  const segMapper = (logText.match(/SEG_MAPPER/g) || []).length;
  const segMapperLines = logText.split('\n').filter(l => l.includes('SEG_MAPPER')).slice(0, 10);

  const summary = {
    tag,
    bug4skip,
    bug4Lines,
    bug5skip,
    bug5Lines,
    segAccess,
    segAccessLines,
    binpatch,
    binpatchLines,
    nullskip,
    nullskipLines,
    segMapperFBB4,
    freeInvalid,
    pebwatch,
    segMapper,
    segMapperLines,
    inittable,
    gotfix,
    findenv,
    vkbootCount,
    vkbootFound: /vkboot/.test(logText) && vkbootCount > 2,
    crashB5D71,
    crashLines,
    logLines: logText.split('\n').length,
  };
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary, null, 2));

  await browser.close();
  server.close();
} finally {
  await slot.release();
}
