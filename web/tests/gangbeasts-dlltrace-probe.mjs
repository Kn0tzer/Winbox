import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { mkdir, writeFile } from 'node:fs/promises';

// Gang Beasts nodrv diagnostic: ?dlltrace=1 logs every DLL open (incl.
// winex11.drv) so we can see WHY the graphics driver never loads and why
// explorer /desktop exits at ~47s. Exits early once nodrv is seen.
const dir = '/home/ubuntu/boxedwine64/test-results/gangbeasts-dlltrace-' + Date.now();
await mkdir(dir, { recursive: true });
const t0 = Date.now();
const el = () => Math.round((Date.now() - t0) / 1000) + 's';

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT gang-dlltrace');

const server = await serve('dist');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const logs = [];
page.on('console', m => logs.push('[' + el() + ']' + m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('[' + el() + ']pageerror: ' + e.message));

const summary = { game: 'gangbeasts-dlltrace', arch: 'x64', t0: new Date().toISOString() };
try {
  const gz = encodeURIComponent('https://kn0tzer.work.gd/games/gangbeasts.zip');
  const exe = encodeURIComponent('Gang Beasts.exe');
  const url = `http://127.0.0.1:${port}/runtime/index.html?gamezip=${gz}&gamezipsize=1314310574&exe=${exe}&session=0&dlltrace=1`;
  await page.goto(url, { timeout: 60000 });
  console.log('LOADED gang dlltrace, up to 8 min, early exit on nodrv');
  let nodrvAt = 0, explorerExitAt = 0, lit = 0;
  for (let i = 0; i < 16; i++) {
    await page.waitForTimeout(30000);
    const txt = logs.join('\n');
    if (!explorerExitAt && /explorer\.exe\/desktop[\s\S]{0,120}exit_group/.test(txt)) { explorerExitAt = Date.now() - t0; console.log(`[${el()}] EXPLORER EXIT`); }
    if (!nodrvAt && /nodrv_CreateWindow/.test(txt)) { nodrvAt = Date.now() - t0; console.log(`[${el()}] NODRV SEEN`); }
    const interesting = logs.filter(l => /winex11|DLLTRACE.*(winex11|x11|opengl32|wined3d|dxgi|d3d11)|XWire.*(accept|close|refus)|nodrv|explorer|SHAPE|load_graphics|No driver/i.test(l));
    const fresh = interesting.slice(-6);
    if (fresh.length) console.log('SIG: ' + fresh.join(' || ').slice(0, 600));
    if (nodrvAt) { console.log('nodrv captured, waiting 60s for aftermath then stopping'); await page.waitForTimeout(60000); break; }
  }
  await page.screenshot({ path: dir + '/gang-dlltrace-final.png' }).catch(() => {});
  Object.assign(summary, { nodrvMs: nodrvAt, explorerExitMs: explorerExitAt, logCount: logs.length });
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
