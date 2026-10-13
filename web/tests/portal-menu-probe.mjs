import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { mkdir, writeFile, copyFile } from 'node:fs/promises';

const tag = 'portal-menu-' + Date.now();
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
  const args = encodeURIComponent(JSON.stringify(['-game', 'portal']));
  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?gamezip=' + gz + '&gamezipsize=3349436598&exe=hl2.exe&args=' + args + '&arch=x86&session=0';
  console.log('GOTO ' + url.slice(0, 100) + '...');
  await page.goto(url);
  console.log('PAGE LOADED - waiting for game (NOT gating on bwRuntime.ready)');

  const getStats = () => page.evaluate(() => {
    try { const s = window.bwVkStats?.(); return s ? { lit: s.litPixels || 0 } : null; }
    catch (e) { return null; }
  }).catch(() => null);

  const checkLog = (re) => logs.find(l => re.test(l));

  // Wait loop: up to 35 minutes. Check every 30s for progress signals.
  const deadline = Date.now() + 35 * 60 * 1000;
  let lastLogCount = 0;
  let hl2Seen = false, d3dSeen = false, bestLit = 0, shotCount = 0;
  let lastProgress = Date.now();

  while (Date.now() < deadline) {
    await page.waitForTimeout(30000);
    await flush().catch(() => {});

    // Check for key signals
    if (!hl2Seen && checkLog(/hl2\.exe|hl2/i)) { hl2Seen = true; console.log('>>> HL2.EXE ACTIVITY DETECTED'); }
    if (!d3dSeen && checkLog(/wined3d|D3D9|d3d9\.dll|Direct3D/i)) { d3dSeen = true; console.log('>>> D3D INITIALIZATION DETECTED'); }

    const st = await getStats();
    if (st && st.lit > bestLit) {
      bestLit = st.lit;
      console.log('>>> LIT PIXELS: ' + bestLit);
      lastProgress = Date.now();
      // Take screenshot when we see significant rendering
      if (bestLit > 1000) {
        shotCount++;
        const shotPath = dir + '/portal-progress-' + shotCount + '-lit' + bestLit + '.png';
        await page.screenshot({ path: shotPath }).catch(() => {});
        console.log('SCREENSHOT: ' + shotPath);
        // Also update the final screenshot path
        await copyFile(shotPath, finalShot).catch(() => {});
      }
    }

    // Log progress every 2 min
    const newLogs = logs.length - lastLogCount;
    if (newLogs > 0) {
      lastLogCount = logs.length;
      lastProgress = Date.now();
      // Look for interesting recent messages
      const recent = logs.slice(-20).join('\n');
      if (/Valve|Source engine|Portal|menu|loading/i.test(recent)) {
        console.log('>>> INTERESTING: ' + logs.slice(-5).join(' | ').slice(0, 300));
      }
    }

    const elapsed = Math.round((Date.now() - (deadline - 35*60*1000)) / 60000);
    console.log('[' + elapsed + 'min] logs=' + logs.length + ' lit=' + bestLit + ' hl2=' + hl2Seen + ' d3d=' + d3dSeen);

    // If we have good rendering and it's stable, take final screenshot and check if it looks like a menu
    if (bestLit > 50000 && Date.now() - lastProgress > 120000) {
      console.log('Rendering stable at ' + bestLit + ' lit pixels, taking final screenshot');
      break;
    }
  }

  // Final screenshot
  await page.screenshot({ path: dir + '/portal-final.png' }).catch(() => {});
  await copyFile(dir + '/portal-final.png', finalShot).catch(() => {});
  console.log('FINAL SCREENSHOT: ' + finalShot);

  summary.bestLitPixels = bestLit;
  summary.hl2Detected = hl2Seen;
  summary.d3dDetected = d3dSeen;
  summary.screenshot = finalShot;
  summary.logCount = logs.length;
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  await flush();
  console.log('DONE ' + JSON.stringify(summary));
} catch (e) {
  summary.fatal = e.message;
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2)).catch(() => {});
  try { await writeFile(dir + '/console.log', logs.join('\n')); } catch (_) {}
  console.error('FATAL', e.message);
  process.exitCode = 1;
} finally {
  try { await browser.close(); } catch (_) {}
  server.close();
  await slot.release();
}
