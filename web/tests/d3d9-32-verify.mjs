import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { mkdir, writeFile } from 'node:fs/promises';

const tag = 'd3d9-32-verify-' + Date.now();
const dir = '/home/ubuntu/boxedwine64/test-results/' + tag;
await mkdir(dir, { recursive: true });

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT ACQUIRED');

const server = await serve('/home/ubuntu/boxedwine64');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
const logs = [];
page.on('console', m => logs.push(m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('pageerror: ' + e.message));

try {
  // Use session=0 with p=tri9-32.exe, like the boot regression test
  // Need to stage the 32-bit D3D9 assets
  const params = new URLSearchParams({
    arch: 'x86',
    session: '0',
    persist: '0',
    fresh: '1',
    p: 'tri9-32.exe'
  });
  const url = `http://127.0.0.1:${port}/dist/runtime/index.html?${params}`;
  console.log('GOTO ' + url.slice(0, 80));
  await page.goto(url, { timeout: 60000 });

  const getStats = () => page.evaluate(() => {
    try { const s = window.bwVkStats?.(); return s ? { lit: s.litPixels || 0 } : null; }
    catch (e) { return null; }
  }).catch(() => null);

  const deadline = Date.now() + 5 * 60 * 1000;
  let bestLit = 0;
  while (Date.now() < deadline) {
    await page.waitForTimeout(10000);
    const st = await getStats();
    if (st && st.lit > bestLit) {
      bestLit = st.lit;
      console.log('LIT: ' + bestLit);
    }
    if (bestLit > 1000) break;
    // Check for errors
    const errs = logs.filter(l => /err:|failed|error/i.test(l)).slice(-3);
    if (errs.length > 0) console.log('ERRS: ' + errs.join(' | ').slice(0, 200));
  }

  console.log('FINAL LIT: ' + bestLit);
  await page.screenshot({ path: dir + '/d3d9-32.png' }).catch(() => {});
  await writeFile(dir + '/console.log', logs.join('\n'));
  await writeFile(dir + '/summary.json', JSON.stringify({ tag, litPixels: bestLit, logCount: logs.length }, null, 2));
  console.log(bestLit > 1000 ? 'D3D9-32 WORKS' : 'D3D9-32 NO RENDER');
} catch (e) {
  console.error('FATAL', e.message);
  process.exitCode = 1;
} finally {
  try { await browser.close(); } catch (_) {}
  server.close();
  await slot.release();
}
