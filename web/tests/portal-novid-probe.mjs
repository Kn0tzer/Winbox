import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { mkdir, writeFile, copyFile } from 'node:fs/promises';

const tag = 'portal-novid-' + Date.now();
const dir = '/home/ubuntu/boxedwine64/test-results/' + tag;
await mkdir(dir, { recursive: true });
const finalShot = '/home/ubuntu/boxedwine64/test-results/portal-menu-' + Date.now() + '.png';

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT ACQUIRED');

const server = await serve('dist');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const logs = [];
page.on('console', m => logs.push(m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('pageerror: ' + e.message));
const flush = () => writeFile(dir + '/console.log', logs.join('\n'));

const summary = { tag, game: 'portal', arch: 'x86' };
try {
  const gz = encodeURIComponent('https://kn0tzer.work.gd/games/portal.zip');
  // hl2.exe with -novid (skip intro videos), -windowed, -game portal
  const args = encodeURIComponent(JSON.stringify(['-game', 'portal', '-novid', '-windowed', '-w', '1280', '-h', '720']));
  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?gamezip=' + gz + '&gamezipsize=3349436598&exe=hl2.exe&args=' + args + '&arch=x86&session=0';
  console.log('GOTO hl2.exe -novid...');
  await page.goto(url);
  console.log('PAGE LOADED');

  const getStats = () => page.evaluate(() => {
    try { const s = window.bwVkStats?.(); return s ? { lit: s.litPixels || 0 } : null; }
    catch (e) { return null; }
  }).catch(() => null);

  const deadline = Date.now() + 30 * 60 * 1000;
  let bestLit = 0, shotCount = 0;

  while (Date.now() < deadline) {
    await page.waitForTimeout(30000);
    await flush().catch(() => {});

    const st = await getStats();
    if (st && st.lit > bestLit) {
      bestLit = st.lit;
      console.log('>>> LIT PIXELS: ' + bestLit);
      if (bestLit > 500) {
        shotCount++;
        const shotPath = dir + '/portal-' + shotCount + '-lit' + bestLit + '.png';
        await page.screenshot({ path: shotPath }).catch(() => {});
        console.log('SCREENSHOT: ' + shotPath);
        await copyFile(shotPath, finalShot).catch(() => {});
      }
    }

    const elapsed = Math.round((Date.now() - (deadline - 30*60*1000)) / 60000);
    console.log('[' + elapsed + 'min] logs=' + logs.length + ' lit=' + bestLit);

    if (bestLit > 20000) {
      console.log('Good rendering, breaking');
      break;
    }
  }

  await page.screenshot({ path: dir + '/portal-final.png' }).catch(() => {});
  await copyFile(dir + '/portal-final.png', finalShot).catch(() => {});
  console.log('FINAL: ' + finalShot);
  summary.bestLitPixels = bestLit;
  summary.screenshot = finalShot;
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  await flush();
  console.log('DONE ' + JSON.stringify(summary));
} catch (e) {
  summary.fatal = e.message;
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2)).catch(() => {});
  console.error('FATAL', e.message);
  process.exitCode = 1;
} finally {
  try { await browser.close(); } catch (_) {}
  server.close();
  await slot.release();
}
