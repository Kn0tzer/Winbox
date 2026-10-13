import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { execFile } from 'node:child_process';

// Celeste progress watch: window maps white at ~41s then guest goes silent.
// Screenshot-hash every 60s + host-side CPU of chrome renderers to tell
// JIT/loading (hot CPU, changing shots) from stuck (idle, static white).
const dir = '/home/ubuntu/boxedwine64/test-results/celeste-watch-' + Date.now();
await mkdir(dir, { recursive: true });
const t0 = Date.now();
const el = () => Math.round((Date.now() - t0) / 1000) + 's';
const cpuOf = () => new Promise(res => {
  execFile('sh', ['-c', 'ps -o %cpu,rss,etime,args -e | grep "[c]hrome.*--headless" | head -n 6'], (e, so) => res(so ? so.trim() : '?'));
});

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT celeste-watch');

const server = await serve('dist');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const logs = [];
page.on('console', m => logs.push('[' + el() + ']' + m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('[' + el() + ']pageerror: ' + e.message));

const summary = { game: 'celeste-watch', arch: 'x86', t0: new Date().toISOString() };
try {
  const gz = encodeURIComponent('https://kn0tzer.work.gd/games/celeste.zip');
  const url = `http://127.0.0.1:${port}/runtime/index.html?gamezip=${gz}&gamezipsize=1423947143&exe=Celeste.exe&arch=x86&session=0`;
  await page.goto(url, { timeout: 60000 });
  console.log('LOADED celeste-watch, 12 min');
  let lastHash = '', changes = 0;
  for (let i = 0; i < 12; i++) {
    await page.waitForTimeout(60000);
    const buf = await page.screenshot().catch(() => null);
    const h = buf ? createHash('sha256').update(buf).digest('hex').slice(0, 16) : 'noshot';
    const changed = h !== lastHash;
    if (changed && lastHash) changes++;
    lastHash = h;
    const cpu = await cpuOf();
    const recent = logs.slice(-4).join(' || ').slice(0, 220);
    console.log(`[${el()}] shot=${h} changed=${changed} changes=${changes} logs=${logs.length} recent=${recent}`);
    console.log(`  chrome-cpu:\n  ` + String(cpu).split('\n').join('\n  '));
    if (changed) await writeFile(dir + `/shot-${i}-${h}.png`, buf).catch(() => {});
  }
  Object.assign(summary, { shotChanges: changes, logCount: logs.length });
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
