import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { mkdir, writeFile } from 'node:fs/promises';

const dir = '/home/ubuntu/boxedwine64/test-results/gangbeasts-boot-' + Date.now();
await mkdir(dir, { recursive: true });
const t0 = Date.now();
const el = () => Math.round((Date.now() - t0) / 1000) + 's';

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT gangbeasts');

const server = await serve('dist');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const logs = [];
page.on('console', m => logs.push('[' + el() + ']' + m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('[' + el() + ']pageerror: ' + e.message));

const summary = { game: 'gangbeasts', arch: 'x64', t0: new Date().toISOString() };
try {
  const gz = encodeURIComponent('https://kn0tzer.work.gd/games/gangbeasts.zip');
  const exe = encodeURIComponent('Gang Beasts.exe');
  const url = `http://127.0.0.1:${port}/runtime/index.html?gamezip=${gz}&gamezipsize=1314310574&exe=${exe}&session=0`;
  await page.goto(url, { timeout: 60000 });
  console.log('LOADED gangbeasts, waiting up to 15 min');
  let firstExe = 0, firstLit = 0, firstUnity = 0, bestLit = 0;
  for (let i = 0; i < 30; i++) {
    await page.waitForTimeout(30000);
    const txt = logs.join('\n');
    if (!firstExe && /gang ?beasts\.exe/i.test(txt)) { firstExe = Date.now() - t0; console.log(`[${el()}] EXE SPAWN`); }
    if (!firstUnity && /unity|UnityPlayer|il2cpp|GameAssembly/i.test(txt)) { firstUnity = Date.now() - t0; console.log(`[${el()}] UNITY ACTIVITY`); }
    const lit = await page.evaluate(() => { try { return window.bwVkStats?.()?.litPixels || 0; } catch(e) { return 0; } }).catch(() => 0);
    if (lit > bestLit) { bestLit = lit; console.log(`[${el()}] lit=${lit}`); }
    if (lit > 0 && !firstLit) { firstLit = Date.now() - t0; console.log(`[${el()}] FIRST FRAME`); await page.screenshot({ path: dir + '/gangbeasts-first.png' }).catch(() => {}); }
    const interesting = logs.filter(l => /gang|unity|d3d|dxvk|vulkan|error|fail|exception/i.test(l)).slice(-3);
    if (interesting.length) console.log('SIG: ' + interesting.join(' || ').slice(0, 400));
    if (bestLit > 50000 && lit > 0) { console.log('Stable rendering, stopping early'); break; }
  }
  await page.screenshot({ path: dir + '/gangbeasts-final.png' }).catch(() => {});
  Object.assign(summary, { firstExeMs: firstExe, firstUnityMs: firstUnity, firstFrameMs: firstLit, bestLitPixels: bestLit, logCount: logs.length });
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  await writeFile(dir + '/console.log', logs.join('\n'));
  console.log('DONE ' + JSON.stringify(summary));
} catch (e) {
  summary.fatal = e.message;
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2)).catch(() => {});
  try { await writeFile(dir + '/console.log', logs.join('\n')); } catch (_) {}
  console.error('FATAL ' + e.message);
  process.exitCode = 1;
} finally {
  try { await browser.close(); } catch (_) {}
  server.close();
  await slot.release();
}
