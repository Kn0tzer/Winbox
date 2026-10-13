import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { mkdir } from 'node:fs/promises';

const dir = '/home/ubuntu/boxedwine64/test-results/portal-shot-' + Date.now();
await mkdir(dir, { recursive: true });
const finalShot = '/home/ubuntu/boxedwine64/test-results/portal-menu-' + Date.now() + '.png';

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT');

const server = await serve('/home/ubuntu/boxedwine64');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
page.on('console', m => { if (/hl2|portal|valve|source|d3d|render/i.test(m.text())) console.log('LOG: ' + m.text().slice(0, 100)); });

try {
  const gz = encodeURIComponent('https://kn0tzer.work.gd/games/portal.zip');
  const args = encodeURIComponent(JSON.stringify(['-game', 'portal', '-novid']));
  const url = `http://127.0.0.1:${port}/dist/runtime/index.html?gamezip=${gz}&gamezipsize=3349436598&exe=hl2.exe&args=${args}&arch=x86&session=0`;
  await page.goto(url, { timeout: 60000 });
  console.log('LOADED, waiting 10 min then screenshot');

  for (let i = 0; i < 20; i++) {
    await page.waitForTimeout(30000);
    const lit = await page.evaluate(() => { try { return window.bwVkStats?.()?.litPixels || 0; } catch(e) { return 0; } }).catch(() => 0);
    console.log(`[${i*0.5}min] lit=${lit}`);
    if (lit > 100) {
      await page.screenshot({ path: finalShot });
      console.log('SCREENSHOT TAKEN: ' + finalShot);
      break;
    }
  }
  // Final screenshot regardless
  await page.screenshot({ path: finalShot });
  await page.screenshot({ path: dir + '/final.png' });
  console.log('FINAL SHOT: ' + finalShot);
} catch (e) {
  console.error('FATAL ' + e.message);
} finally {
  try { await browser.close(); } catch (_) {}
  server.close();
  await slot.release();
}
