import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';

// Gang Beasts long boot: the game loads slowly (100+MB over range-fetch) and
// spawns helper instances that exit early. Run 20 min, screenshot every 60s,
// hash screenshots to detect visual progress on ANY render path (GL/Vulkan).
const dir = '/home/ubuntu/boxedwine64/test-results/gangbeasts-long-' + Date.now();
await mkdir(dir, { recursive: true });
const t0 = Date.now();
const el = () => Math.round((Date.now() - t0) / 1000) + 's';

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT gang-long');

const server = await serve('dist');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const logs = [];
page.on('console', m => logs.push('[' + el() + ']' + m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('[' + el() + ']pageerror: ' + e.message));

const summary = { game: 'gangbeasts-long', arch: 'x64', t0: new Date().toISOString() };
try {
  const gz = encodeURIComponent('https://kn0tzer.work.gd/games/gangbeasts.zip');
  const exe = encodeURIComponent('Gang Beasts.exe');
  const url = `http://127.0.0.1:${port}/runtime/index.html?gamezip=${gz}&gamezipsize=1314310574&exe=${exe}&session=0`;
  await page.goto(url, { timeout: 60000 });
  console.log('LOADED gang-long, 20 min with screenshot hashing');
  let lastHash = '', changes = 0, nodrvAt = 0, gameExitAt = 0;
  for (let i = 0; i < 20; i++) {
    await page.waitForTimeout(60000);
    const buf = await page.screenshot().catch(() => null);
    const h = buf ? createHash('sha256').update(buf).digest('hex').slice(0, 16) : 'noshot';
    const changed = h !== lastHash;
    if (changed && lastHash) { changes++; }
    lastHash = h;
    const txt = logs.join('\n');
    if (!nodrvAt && /nodrv_CreateWindow/.test(txt)) { nodrvAt = Date.now() - t0; console.log(`[${el()}] NODRV`); }
    const exits = (txt.match(/Gang Beasts\.exe[\s\S]{0,80}exit_group/g) || []).length;
    if (!gameExitAt && /Gang Beasts\.exe[\s\S]{0,80}exit_group/.test(txt)) { gameExitAt = Date.now() - t0; }
    const lit = await page.evaluate(() => { try { return window.bwVkStats?.()?.litPixels || 0; } catch(e) { return 0; } }).catch(() => 0);
    console.log(`[${el()}] shot=${h} changed=${changed} changes=${changes} lit=${lit} exits=${exits} logs=${logs.length}`);
    if (changed) await writeFile(dir + `/shot-${i}-${h}.png`, buf).catch(() => {});
    const unity = logs.filter(l => /Unity|d3d11|dxgi|winex11|nodrv|CrashHandler|Physics/i.test(l)).slice(-2);
    if (unity.length) console.log('  SIG: ' + unity.join(' || ').slice(0, 300));
  }
  Object.assign(summary, { shotChanges: changes, nodrvMs: nodrvAt, gameExitMs: gameExitAt, logCount: logs.length });
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
