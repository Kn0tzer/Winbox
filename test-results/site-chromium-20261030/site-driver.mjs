import { chromium } from '@playwright/test';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022/web/tests/probe-slots.mjs';
import { serve } from '/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022/tools/browser/serve.mjs';
import { mkdirSync, writeFileSync, appendFileSync, readdirSync, readFileSync } from 'node:fs';

const serveDir = process.env.SERVE_DIR || '';
const outDir = process.env.OUT_DIR || '/tmp/site-driver/out';
const profileDir = process.env.PROFILE_DIR || '/tmp/site-driver/profile';
const demoId = process.env.DEMO_ID || 'chromium';
const timeoutMs = Number(process.env.TIMEOUT_MS || 600000);
const frameWaitMs = Number(process.env.FRAME_WAIT_MS || 180000);
mkdirSync(outDir, { recursive: true });
function log(...a) {
  const line = `[${new Date().toISOString()}] ` + a.join(' ');
  console.log(line);
  appendFileSync(outDir + '/events.log', line + '\n');
}

function profileCpuJiffies() {
  let total = 0;
  try {
    for (const pid of readdirSync('/proc')) {
      if (!/^\d+$/.test(pid)) continue;
      let cmd;
      try { cmd = readFileSync(`/proc/${pid}/cmdline`, 'utf8'); } catch { continue; }
      if (!cmd.includes(profileDir)) continue;
      try {
        const stat = readFileSync(`/proc/${pid}/stat`, 'utf8');
        const tail = stat.slice(stat.lastIndexOf(')') + 2).split(' ');
        const utime = Number(tail[11]), stime = Number(tail[12]);
        if (Number.isFinite(utime + stime)) total += utime + stime;
      } catch { }
    }
  } catch { }
  return total;
}

let server = null;
let url = process.env.SITE_URL || '';
if (serveDir) {
  server = await serve(serveDir);
  url = `http://127.0.0.1:${server.address().port}/`;
}
log('target url:', url, 'demo:', demoId);

const slot = await acquireProbeSlot({ timeoutMs: 1800000 });
log('slot acquired');
let browser;
let outcome = 'timeout';
try {
  browser = await chromium.launchPersistentContext(profileDir, {
    executablePath: '/usr/local/bin/chromium',
    headless: true,
    viewport: { width: 1440, height: 1000 },
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
  });
  const page = browser.pages()[0] || await browser.newPage();
  let crashed = false;
  page.on('crash', () => { crashed = true; log('PAGE CRASH EVENT'); });
  page.on('console', m => {
    if (m.type() === 'error' || m.type() === 'warning') appendFileSync(outDir + '/console.log', `${m.type()}: ${m.text().slice(0, 500)}\n`);
  });
  page.on('pageerror', e => appendFileSync(outDir + '/console.log', 'PAGEERROR: ' + String(e).slice(0, 500) + '\n'));
  page.on('response', r => { if (r.status() >= 400) appendFileSync(outDir + '/console.log', `HTTP ${r.status()} ${r.url().slice(0, 220)}\n`); });
  await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 120000 });
  await page.waitForSelector(`button[data-game="${demoId}"]`, { timeout: 120000 });
  log('catalog loaded; crossOriginIsolated =', String(await page.evaluate(() => window.crossOriginIsolated)));
  await page.screenshot({ path: outDir + '/00-catalog.png' });
  const t0 = Date.now();
  await page.click(`button[data-game="${demoId}"]`);
  log('clicked demo', demoId);
  let lastStatus = '';
  let lastLogLen = -1;
  let lastLogChangeAt = Date.now();
  let shots = 0;
  let lastShotAt = 0;
  let mappedAt = 0;
  let prevCpu = profileCpuJiffies();
  let prevCpuAt = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    if (crashed) { outcome = mappedAt ? 'tab-crashed-after-map' : 'tab-crashed'; break; }
    await page.waitForTimeout(8000);
    if (crashed) { outcome = mappedAt ? 'tab-crashed-after-map' : 'tab-crashed'; break; }
    const snap = await page.evaluate(() => {
      const $ = id => document.getElementById(id);
      const frame = document.querySelector('#display iframe');
      let ready = null;
      try { ready = frame?.contentWindow?.bwRuntime?.ready?.() ?? null; } catch (e) { }
      return {
        status: $('status')?.textContent || '',
        loadingHidden: $('loading') ? $('loading').hidden : null,
        logFull: $('log')?.textContent || '',
        ready,
        canvases: [...document.querySelectorAll('#display canvas')].map(c => ({ id: c.id, w: c.width, h: c.height })),
      };
    }).catch(e => ({ error: String(e) }));
    const nowCpu = profileCpuJiffies();
    const nowAt = Date.now();
    const cpuCores = ((nowCpu - prevCpu) / 100) / ((nowAt - prevCpuAt) / 1000);
    prevCpu = nowCpu; prevCpuAt = nowAt;
    if (snap.error) {
      if (/Target crashed/i.test(snap.error)) { outcome = mappedAt ? 'tab-crashed-after-map' : 'tab-crashed'; log('evaluate reports target crashed'); }
      appendFileSync(outDir + '/snaps.jsonl', JSON.stringify({ t: Date.now() - t0, error: snap.error, cpuCores: Number(cpuCores.toFixed(3)) }) + '\n');
      if (outcome.includes('crashed')) break;
      continue;
    }
    if (snap.status && snap.status !== lastStatus) { log('STATUS:', snap.status); lastStatus = snap.status; }
    if (snap.logFull.length !== lastLogLen) { lastLogLen = snap.logFull.length; lastLogChangeAt = Date.now(); }
    writeFileSync(outDir + '/logfull.txt', snap.logFull);
    if (!mappedAt && /first window mapped/i.test(snap.logFull)) { mappedAt = Date.now(); log('WINDOW MAPPED at t=', Math.round((mappedAt - t0) / 1000), 's'); }
    appendFileSync(outDir + '/snaps.jsonl', JSON.stringify({ t: Date.now() - t0, status: snap.status, ready: snap.ready, loadingHidden: snap.loadingHidden, logLen: snap.logFull.length, logSilentForS: Math.round((Date.now() - lastLogChangeAt) / 1000), cpuCores: Number(cpuCores.toFixed(3)), mapped: !!mappedAt, canvases: snap.canvases }) + '\n');
    const shotEvery = mappedAt ? 15000 : 30000;
    if (Date.now() - lastShotAt >= shotEvery && shots < 40) {
      shots++; lastShotAt = Date.now();
      await page.screenshot({ path: `${outDir}/shot-${String(shots).padStart(2, '0')}.png` }).catch(() => { });
    }
    if (mappedAt && snap.loadingHidden === true) { outcome = 'window-mapped+frame'; break; }
    if (mappedAt && Date.now() - mappedAt > frameWaitMs) { outcome = 'window-mapped-noframe'; break; }
  }
  await page.waitForTimeout(3000).catch(() => { });
  await page.screenshot({ path: outDir + '/final.png' }).catch(() => { });
  log('OUTCOME:', outcome);
  writeFileSync(outDir + '/outcome.txt', outcome + '\n');
} catch (e) {
  log('DRIVER ERROR:', String(e).slice(0, 500));
  writeFileSync(outDir + '/outcome.txt', 'driver-error: ' + String(e).slice(0, 300) + '\n');
} finally {
  await browser?.close().catch(() => { });
  await slot.release();
  server?.close();
  log('slot released');
}
