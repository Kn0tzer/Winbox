// gb-menu-probe.mjs -- drive Gang Beasts from its title toward a started
// round, with screenshot evidence at every step (keyboard + GB-menu lane).
// Mapping wait is death-aware (D3D12 stack-merge adoption of the
// frame-floor rig-fix lane, commit 6f3831f26, web/tests/probe-guest-death.mjs):
// a dead guest aborts promptly with a GUEST-DIED verdict instead of
// burning the 3600s mapping timeout the way the 2026-10-27 boundary
// run did (~50 min wasted after the game died at ~622s).
//
// Method: launch through the REAL shell (demo-matrix helpers), wait for
// presented frames, then run a patient input campaign. Round 1 uses the
// product input path (mouse click into the display for iframe focus, then
// Playwright keyboard). If no step changes the screen, round 2 repeats the
// campaign with native bw64_xwire_key injection into the runtime frame
// (bypasses DOM focus entirely). Every step records a screenshot + pixel
// stats + tier counters, so "the menu advanced" is a measured screen change,
// not an impression. Env: TAG, OUT, MAXMIN (soft budget minutes).
import { chromium } from '@playwright/test';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { applyJitSetting } from './demo-matrix.mjs';
import { createGuestDeathWatch } from './probe-guest-death.mjs';
import { screenshotPixels } from './suite/lib/pixels.mjs';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
process.chdir(repo);
const tag = process.env.TAG || `menu-${Date.now()}`;
const outDir = resolve(repo, process.env.OUT || `test-results/gb-menu-${tag}`);
const startedAt = Date.now();
const elapsed = () => (Date.now() - startedAt) / 1000;
const consoleLines = [];
const steps = [];
let slot, server, browser, page, demosOrig = null;
const deathWatch = createGuestDeathWatch();
let guestDeath = null;
const noteDeath = () => {
  const d = deathWatch.check(elapsed() * 1000);
  if (d && !guestDeath) {
    guestDeath = d;
    consoleLines.push(`[${elapsed().toFixed(1)}s] GUEST-DIED kind=${d.kind} sig=${String(d.signature).slice(0, 160)}`);
    console.log('GUEST-DIED', d.kind, String(d.signature).slice(0, 160));
  }
  return guestDeath;
};

const sha = buf => createHash('sha256').update(buf).digest('hex').slice(0, 16);
// Coarse 32x18 average-color grid of a screenshot, decoded in-page. Ambient
// backdrop animation moves this a little; a screen transition moves it a
// lot. gridDiff = mean abs per-cell per-channel difference (0..255).
async function gridOf(pngBuf) {
  const b64 = pngBuf.toString('base64');
  return page.evaluate(async (b) => {
    const img = new Image();
    await new Promise((res, rej) => { img.onload = res; img.onerror = rej; img.src = 'data:image/png;base64,' + b; });
    const c = document.createElement('canvas'); c.width = 32; c.height = 18;
    const ctx = c.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(img, 0, 0, 32, 18);
    const d = ctx.getImageData(0, 0, 32, 18).data;
    const out = [];
    for (let i = 0; i < d.length; i += 4) out.push(d[i], d[i + 1], d[i + 2]);
    return out;
  }, b64).catch(() => null);
}
const gridDiff = (a, b) => {
  if (!a || !b || a.length !== b.length) return null;
  let sum = 0; for (let i = 0; i < a.length; i++) sum += Math.abs(a[i] - b[i]);
  return +(sum / a.length).toFixed(1);
};
async function snap(label) {
  const png = await page.screenshot({ fullPage: true }).catch(() => null);
  let rec = { label, t: +elapsed().toFixed(1) };
  if (png) {
    await writeFile(join(outDir, `${label}.png`), png);
    rec.pngSha = sha(png);
    try { const px = screenshotPixels(png); rec.colors = px.distinctColors ?? px.uniqueColors ?? null; rec.lit = px.litPixels ?? px.lit ?? null; }
    catch (e) { rec.pxError = e.message; }
  }
  const st = await page.evaluate(() => { try { const s = window.bwVkStats?.(); return s ? { rendered: s.v2?.rendered, received: s.v2?.framesReceived, lit: s.litPixels } : null; } catch { return null; } }).catch(() => null);
  rec.tier = st;
  if (png) { rec.grid = await gridOf(png); const prevRec = steps[steps.length - 1]; const firstRec = steps[0];
    rec.diffPrev = prevRec && prevRec.grid ? gridDiff(prevRec.grid, rec.grid) : null;
    rec.diffTitle = firstRec && firstRec.grid ? gridDiff(firstRec.grid, rec.grid) : null; }
  steps.push(rec);
  consoleLines.push(`[${rec.t}s] STEP ${label} sha=${rec.pngSha} colors=${rec.colors} tier=${JSON.stringify(st)}`);
  console.log('STEP', label, rec.pngSha, rec.colors, 'diffPrev', rec.diffPrev, 'diffTitle', rec.diffTitle, JSON.stringify(st));
  return rec;
}
async function statsSnap() {
  return page.evaluate(() => { try { const s = window.bwVkStats?.(); return s ? { rendered: s.v2?.rendered ?? null, lit: s.litPixels ?? null } : null; } catch { return null; } }).catch(() => null);
}
async function waitRenders(min, timeoutS) {
  const end = Date.now() + timeoutS * 1000;
  while (Date.now() < end) { if (noteDeath()) return statsSnap(); const s = await statsSnap(); if ((s?.rendered ?? 0) >= min) return s; await page.waitForTimeout(5000); }
  return statsSnap();
}
const changed = (a, b) => a && b && a.pngSha !== b.pngSha;

async function main() {
  await mkdir(outDir, { recursive: true });
  // Point the shell at the local games mirror; restore dist bytes at the end.
  const demosPath = join(repo, 'dist/demos.mjs');
  demosOrig = await readFile(demosPath);
  const patched = demosOrig.toString('utf8').replace('https://kn0tzer.work.gd/games/', 'http://127.0.0.1:8901/');
  if (patched === demosOrig.toString('utf8')) throw new Error('demos.mjs archiveBase not found');
  await writeFile(demosPath, patched);

  await waitForBuildIdle();
  slot = await acquireProbeSlot();
  server = await serve(process.env.BW_TEST_DIST || 'dist');
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
  });
  page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  await page.addInitScript(() => { window.__bwStatsEvery = 1; });
  page.on('console', msg => {
    const text = msg.text();
    consoleLines.push(`[${elapsed().toFixed(1)}s] ${msg.type()}: ${text}`);
    deathWatch.feed({ t: elapsed() * 1000, type: msg.type(), text });
    noteDeath();
  });
  page.on('pageerror', err => {
    consoleLines.push(`[${elapsed().toFixed(1)}s] pageerror: ${err.message}`);
    deathWatch.feedPageError(err.message, elapsed() * 1000);
    noteDeath();
  });
  const baseUrl = `http://127.0.0.1:${server.address().port}/`;
  await page.goto(baseUrl, { waitUntil: 'domcontentloaded' });
  await applyJitSetting(page, '1');
  // Death-aware mapping wait: click the demo, then poll the same
  // 'first window mapped' condition launchDemoGame waits on in 1s
  // slices, checking the guest-death watch each slice. A dead guest
  // aborts here instead of burning the 3600s mappedTimeout.
  await page.locator('[data-game="gangbeasts"]').click();
  {
    const deadline = Date.now() + 3600 * 1000;
    let mapped = false;
    while (Date.now() < deadline) {
      if (noteDeath()) break;
      try { mapped = await page.evaluate(() => document.getElementById('log')?.textContent.includes('first window mapped')); } catch {}
      if (mapped) break;
      await page.waitForTimeout(1000);
    }
    if (guestDeath) {
      const summary = { guestDeath, advancedAt: null, method: null, steps };
      await writeFile(join(outDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
      console.log('GBMENU', JSON.stringify({ guestDeath: guestDeath.kind, advancedAt: null }));
      return;
    }
    if (!mapped) throw new Error('first window mapped timeout 3600000ms exceeded');
  }
  consoleLines.push(`[${elapsed().toFixed(1)}s] probe: window mapped`);
  console.log("MAPPED at", elapsed().toFixed(0));
  await waitRenders(3, 1800);
  console.log("RENDERS-OK at", elapsed().toFixed(0));
  await page.waitForTimeout(30000); // let the title settle
  const box = await page.locator('#display').boundingBox();
  const cx = box.x + box.width / 2, cy = box.y + box.height / 2;
  const clickDisplay = async () => { await page.mouse.click(cx, cy); await page.waitForTimeout(1200); };
  const key = async (k, waitS = 5) => { await page.keyboard.press(k); await page.waitForTimeout(waitS * 1000); };

  let prev = await snap('s00-title');
  const campaign = [
    ['s01-click', async () => clickDisplay()],
    ['s02-enter', async () => key('Enter', 8)],
    ['s03-space', async () => key('Space', 8)],
    ['s04-s-nav', async () => key('s', 3)],
    ['s05-enter2', async () => key('Enter', 10)],
    ['s06-w-nav', async () => key('w', 3)],
    ['s07-space2', async () => key('Space', 10)],
    ['s08-down', async () => key('ArrowDown', 3)],
    ['s09-enter3', async () => key('Enter', 10)],
    ['s10-click2', async () => clickDisplay()],
    ['s11-space3', async () => key('Space', 12)],
    ['s12-enter4', async () => key('Enter', 12)],
  ];
  let advancedAt = null;
  for (const [label, act] of campaign) {
    await act();
    const cur = await snap(label);
    console.log("STEP", label, "diffTitle", cur.diffTitle);
    if (!advancedAt && (cur.diffTitle ?? 0) > 25 && label !== 's01-click' && label !== 's10-click2') advancedAt = label;
    prev = cur;
  }
  const summary = { advancedAt, method: advancedAt ? 'dom' : null, steps };

  if (!advancedAt) {
    // Round 2: native injection into the runtime frame.
    consoleLines.push(`[${elapsed().toFixed(1)}s] probe: DOM campaign produced no screen change; native round`);
    console.log('NATIVE ROUND');
    const frameEl = await page.locator('#display iframe').elementHandle();
    const rt = await frameEl.contentFrame();
    const sendKey = (sc, down) => rt.evaluate(([c, d]) => window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [c, d]), [sc, down ? 1 : 0]);
    const nkey = async (sc, waitS = 8) => { await sendKey(sc, 1); await page.waitForTimeout(300); await sendKey(sc, 0); await page.waitForTimeout(waitS * 1000); };
    await clickDisplay();
    const ncampaign = [
      ['n01-enter', 40], ['n02-space', 44], ['n03-s', 22], ['n04-enter', 40],
      ['n05-w', 26], ['n06-space', 44], ['n07-down', 81], ['n08-enter', 40], ['n09-space', 44],
    ];
    for (const [label, sc] of ncampaign) {
      await nkey(sc, label.endsWith('space') || label.endsWith('enter') ? 10 : 4);
      const cur = await snap(label);
    console.log("STEP", label, "diffTitle", cur.diffTitle);
      if (!advancedAt && (cur.diffTitle ?? 0) > 25) { advancedAt = label; summary.method = 'native'; }
      prev = cur;
    }
    summary.advancedAt = advancedAt;
  }

  // If anything advanced, try to push toward a round: join (Space = jump) then
  // confirm (Enter), then gameplay keys, screenshotting throughout.
  if (advancedAt) {
    const useNative = summary.method === 'native';
    const frameEl = useNative ? await page.locator('#display iframe').elementHandle() : null;
    const rt = useNative ? await frameEl.contentFrame() : null;
    const sendKey = useNative ? (sc, down) => rt.evaluate(([c, d]) => window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [c, d]), [sc, down ? 1 : 0]) : null;
    const press = async (domKey, sc, waitS = 10) => {
      if (useNative) { await sendKey(sc, 1); await page.waitForTimeout(300); await sendKey(sc, 0); }
      else await page.keyboard.press(domKey);
      await page.waitForTimeout(waitS * 1000);
    };
    await press('Space', 44, 10); await snap('r01-join-space');
    await press('Enter', 40, 12); await snap('r02-start-enter');
    await press('Space', 44, 12); await snap('r03-start-space');
    await press('w', 26, 2); await snap('r04-move-w');
    await press('Space', 44, 6); await snap('r05-jump');
    await waitRenders((await statsSnap())?.rendered + 1, 60);
    await snap('r06-settled');
    summary.roundPush = true;
  }

  for (const r of steps) delete r.grid;
  summary.finalTier = await statsSnap();
  await writeFile(join(outDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
  await writeFile(join(outDir, 'console.txt'), consoleLines.join('\n') + '\n');
  console.log('GBMENU', JSON.stringify({ advancedAt: summary.advancedAt, method: summary.method, roundPush: !!summary.roundPush }));
}

try { await main(); }
finally {
  await writeFile(join(outDir, 'console.txt'), consoleLines.join('\n') + '\n').catch(() => {});
  if (demosOrig) await writeFile(join(repo, 'dist/demos.mjs'), demosOrig).catch(() => {});
  const after = demosOrig ? sha(await readFile(join(repo, 'dist/demos.mjs'))) : null;
  console.log('demos.mjs restored sha16', after);
  await browser?.close().catch(() => {});
  if (server) await new Promise(r => server.close(r)).catch(() => {});
  await slot?.release();
}
