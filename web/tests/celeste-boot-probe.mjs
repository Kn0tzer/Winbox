import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { mkdir, writeFile } from 'node:fs/promises';

const dir = '/home/ubuntu/boxedwine64/test-results/celeste-boot-' + Date.now();
await mkdir(dir, { recursive: true });
const t0 = Date.now();
const el = () => Math.round((Date.now() - t0) / 1000) + 's';

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT celeste');

const server = await serve('dist');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const logs = [];
page.on('console', m => logs.push('[' + el() + ']' + m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('[' + el() + ']pageerror: ' + e.message));

const summary = { game: 'celeste', arch: 'x86', t0: new Date().toISOString() };
try {
  const gz = encodeURIComponent('https://kn0tzer.work.gd/games/celeste.zip');
  const url = `http://127.0.0.1:${port}/runtime/index.html?gamezip=${gz}&gamezipsize=1423947143&exe=Celeste.exe&arch=x86&session=0`;
  await page.goto(url, { timeout: 60000 });
  console.log('LOADED celeste, waiting up to 15 min');
  let firstExe = 0, firstLit = 0, firstAudio = 0, bestLit = 0;
  for (let i = 0; i < 30; i++) {
    await page.waitForTimeout(30000);
    const txt = logs.join('\n');
    if (!firstExe && /celeste\.exe/i.test(txt)) { firstExe = Date.now() - t0; console.log(`[${el()}] EXE SPAWN`); }
    if (!firstAudio && /fmod|dsound|winmm|audio/i.test(txt)) { firstAudio = Date.now() - t0; console.log(`[${el()}] AUDIO ACTIVITY`); }
    const lit = await page.evaluate(() => { try { return window.bwVkStats?.()?.litPixels || 0; } catch(e) { return 0; } }).catch(() => 0);
    if (lit > bestLit) { bestLit = lit; console.log(`[${el()}] lit=${lit}`); }
    if (lit > 0 && !firstLit) { firstLit = Date.now() - t0; console.log(`[${el()}] FIRST FRAME`); await page.screenshot({ path: dir + '/celeste-first.png' }).catch(() => {}); }
    const interesting = logs.filter(l => /celeste|fna|sdl|opengl|\bgl\b|error|fail|exception/i.test(l)).slice(-3);
    if (interesting.length) console.log('SIG: ' + interesting.join(' || ').slice(0, 400));
    if (bestLit > 50000 && lit > 0) { console.log('Stable rendering, stopping early'); break; }
  }
  await page.screenshot({ path: dir + '/celeste-final.png' }).catch(() => {});
  Object.assign(summary, { firstExeMs: firstExe, firstAudioMs: firstAudio, firstFrameMs: firstLit, bestLitPixels: bestLit, logCount: logs.length });
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
