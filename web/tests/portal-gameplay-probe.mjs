// Portal gameplay probe: main menu -> NEW GAME -> first chamber, with
// screenshot/diff input proof and presented-frame rate measurement.
// Records observations only; interpretation lives in the lane report.
import { chromium } from '@playwright/test';
import { appendFile, mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';
import { serve } from '../../tools/browser/serve.mjs';
import { screenshotPixels } from './suite/lib/pixels.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { applyJitSetting, launchDemoGame } from './demo-matrix.mjs';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
process.chdir(repo);

const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/T/, 'T').slice(0, 15);
const outDir = resolve(repo, process.env.OUT || join('test-results/portal-gameplay', `run-${stamp}`));
const mappedTimeout = Number(process.env.MAPPED_TIMEOUT_S || 900);
const menuWaitS = Number(process.env.MENU_WAIT_S || 1500);
const loadWaitS = Number(process.env.LOAD_WAIT_S || 1200);
const freerunS = Number(process.env.FREERUN_S || 180);
const NG = { x: Number(process.env.NG_X || 170), y: Number(process.env.NG_Y || 550) };
const CH = { x: Number(process.env.CH_X || 500), y: Number(process.env.CH_Y || 300) };
const ST = { x: Number(process.env.ST_X || 900), y: Number(process.env.ST_Y || 745) };
const overallDeadline = Date.now() + Number(process.env.OVERALL_MIN || 110) * 60 * 1000;

const metricsPath = join(outDir, 'metrics.jsonl');
let flushedConsole = 0;
async function flushConsole() {
  if (consoleLines.length > flushedConsole) {
    await appendFile(join(outDir, 'console-live.txt'), consoleLines.slice(flushedConsole).join('\n') + '\n').catch(() => {});
    flushedConsole = consoleLines.length;
  }
}
const startedAt = Date.now();
const elapsed = () => Number(((Date.now() - startedAt) / 1000).toFixed(1));
const consoleLines = [];
const steps = [];
let pageErrors = 0;
let consoleErrors = 0;
let lastWasmPresent = null;
let sampling = false;
let samplerPromise = null;
let page, browser, server, slot, build = null;

function decodePng(png) {
  let width, height, channels, position = 8;
  const chunks = [];
  while (position + 12 <= png.length) {
    const size = png.readUInt32BE(position);
    const type = png.toString('ascii', position + 4, position + 8);
    const bytes = png.subarray(position + 8, position + 8 + size);
    if (type === 'IHDR') { width = bytes.readUInt32BE(0); height = bytes.readUInt32BE(4); channels = ({ 2: 3, 6: 4 })[bytes[9]]; }
    else if (type === 'IDAT') chunks.push(bytes);
    else if (type === 'IEND') break;
    position += size + 12;
  }
  const stride = width * channels;
  const raw = inflateSync(Buffer.concat(chunks));
  const out = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const rowStart = y * (stride + 1);
    const filter = raw[rowStart];
    const row = raw.subarray(rowStart + 1, rowStart + 1 + stride);
    const prev = y > 0 ? out.subarray((y - 1) * stride, y * stride) : null;
    const cur = out.subarray(y * stride, (y + 1) * stride);
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? cur[x - channels] : 0;
      const b = prev ? prev[x] : 0;
      const c = (prev && x >= channels) ? prev[x - channels] : 0;
      let v = row[x];
      if (filter === 1) v = (v + a) & 0xff;
      else if (filter === 2) v = (v + b) & 0xff;
      else if (filter === 3) v = (v + ((a + b) >> 1)) & 0xff;
      else if (filter === 4) {
        const p = a + b - c;
        const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        const pr = pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
        v = (v + pr) & 0xff;
      }
      cur[x] = v;
    }
  }
  return { width, height, channels, data: out };
}

// Fraction of sampled pixels that changed, mean delta, and the horizontal
// shift that best aligns A onto B (view yaw right => scene moves left => s>0).
// region {x0,y0,x1,y1} (fractions) restricts the pixel-diff part to a sub-rect.
function diffStats(pngA, pngB, { sample = 2, threshold = 12, region = null } = {}) {
  const a = decodePng(pngA), b = decodePng(pngB);
  if (a.width !== b.width || a.height !== b.height)
    return { error: 'size mismatch', aSize: [a.width, a.height], bSize: [b.width, b.height] };
  const rx0 = region ? Math.floor(region.x0 * a.width) : 0;
  const ry0 = region ? Math.floor(region.y0 * a.height) : 0;
  const rx1 = region ? Math.ceil(region.x1 * a.width) : a.width;
  const ry1 = region ? Math.ceil(region.y1 * a.height) : a.height;
  let changed = 0, total = 0, sumDelta = 0;
  for (let y = ry0; y < ry1; y += sample) {
    for (let x = rx0; x < rx1; x += sample) {
      const ia = (y * a.width + x) * a.channels;
      const ib = (y * b.width + x) * b.channels;
      const d = Math.max(Math.abs(a.data[ia] - b.data[ib]), Math.abs(a.data[ia + 1] - b.data[ib + 1]), Math.abs(a.data[ia + 2] - b.data[ib + 2]));
      sumDelta += d; total++;
      if (d > threshold) changed++;
    }
  }
  const prof = img => {
    const p = new Float64Array(img.width);
    for (let y = 0; y < img.height; y += 4)
      for (let x = 0; x < img.width; x++) {
        const i = (y * img.width + x) * img.channels;
        p[x] += img.data[i] * 0.299 + img.data[i + 1] * 0.587 + img.data[i + 2] * 0.114;
      }
    return p;
  };
  const pa = prof(a), pb = prof(b);
  let bestShift = 0, bestErr = Infinity;
  for (let s = -80; s <= 80; s += 2) {
    let err = 0, n = 0;
    for (let x = 120; x < a.width - 120; x += 4) {
      const bx = x + s;
      if (bx < 0 || bx >= b.width) continue;
      const d = pa[x] - pb[bx];
      err += d * d; n++;
    }
    if (n && err / n < bestErr) { bestErr = err / n; bestShift = s; }
  }
  return {
    changedFraction: Number((changed / total).toFixed(4)),
    meanAbsDelta: Number((sumDelta / total).toFixed(2)),
    bestHorizontalShiftPx: bestShift,
    width: a.width, height: a.height,
  };
}

async function displayClip() {
  const box = await page.locator('#display').boundingBox();
  if (!box) throw new Error('#display has no bounding box');
  return {
    x: Math.max(0, Math.floor(box.x)), y: Math.max(0, Math.floor(box.y)),
    width: Math.max(1, Math.ceil(box.width)), height: Math.max(1, Math.ceil(box.height)),
  };
}

async function clipShot(name) {
  const clip = await displayClip();
  const buf = await page.screenshot({ type: 'png', clip });
  if (name) await writeFile(join(outDir, name), buf);
  return buf;
}

async function fullShot(name) {
  const buf = await page.screenshot({ type: 'png', fullPage: true });
  if (name) await writeFile(join(outDir, name), buf);
  return buf;
}

async function pixelStatsOf(buf) {
  try { const s = screenshotPixels(buf); return { lit: s.lit, varied: s.varied, nonDark: s.nonDarkPixelFraction, colors: s.distinctColorCount }; }
  catch (e) { return { error: e.message }; }
}

async function recordStep(name, extra = {}) {
  const rec = { name, atSeconds: elapsed(), ...extra };
  steps.push(rec);
  console.log('STEP', JSON.stringify(rec));
  return rec;
}

async function sampleMetrics() {
  await flushConsole().catch(() => {});
  const pageTier = await page.evaluate(() => {
    try { return window.bwVkTier?.metrics?.() || null; } catch (e) { return { error: e.message }; }
  }).catch(() => null);
  const rec = { atSeconds: elapsed(), wasmPresent: lastWasmPresent, pageTier };
  await appendFile(metricsPath, JSON.stringify(rec) + '\n').catch(() => {});
  return rec;
}

function startSampler() {
  sampling = true;
  samplerPromise = (async () => {
    while (sampling) {
      await sampleMetrics().catch(() => {});
      await new Promise(r => setTimeout(r, 5000));
    }
  })();
}

const clickHoldMs = Number(process.env.CLICK_HOLD_MS || 1200);
async function clickAt(pt, label, holdMs = clickHoldMs) {
  // Hold the button across at least one guest frame: at ~2fps an instant
  // down+up can fall entirely between two guest frames and never be sampled.
  await page.mouse.move(pt.x, pt.y, { steps: 8 });
  await page.waitForTimeout(400);
  await page.mouse.down();
  try { await page.waitForTimeout(holdMs); } finally { await page.mouse.up(); }
  console.log(`CLICK ${label} at ${pt.x},${pt.y} hold=${holdMs}ms t=${elapsed()}s`);
}

async function holdKey(key, ms) {
  await page.keyboard.down(key);
  try { await page.waitForTimeout(ms); } finally { await page.keyboard.up(key); }
}

// --- DOM input instrumentation: loggers in the launcher document and in the
// runtime iframe record every mouse/key event they actually receive, so a
// dead input path is located exactly (top page vs iframe vs guest). ---
async function runtimeFrame() {
  const handle = await page.locator('#display iframe').elementHandle().catch(() => null);
  return handle ? await handle.contentFrame().catch(() => null) : null;
}

async function installInputLoggers() {
  const install = () => {
    window.__bwInputLog = [];
    for (const type of ['mousemove', 'mousedown', 'mouseup', 'click', 'keydown', 'keyup']) {
      document.addEventListener(type, e => {
        try {
          window.__bwInputLog.push({
            t: type, x: e.clientX, y: e.clientY, key: e.key || null,
            tgt: (e.target?.id || e.target?.tagName || '?') + (e.target?.className ? '.' + String(e.target.className).slice(0, 40) : ''),
          });
        } catch {}
      }, true);
    }
    return 'installed';
  };
  await page.evaluate(install).catch(() => null);
  for (let i = 0; i < 12; i++) {
    const frame = await runtimeFrame();
    if (frame) {
      const ok = await frame.evaluate(install).catch(() => null);
      if (ok === 'installed') return true;
    }
    await page.waitForTimeout(3000);
  }
  return false;
}

async function probeEventStack(pt) {
  return await page.evaluate(([x, y]) => {
    const els = document.elementsFromPoint(x, y).map(e => (e.id || e.tagName) + (e.className ? '.' + String(e.className).slice(0, 30) : ''));
    const ifr = document.querySelector('#display iframe');
    const r = ifr?.getBoundingClientRect();
    return { stack: els, iframeRect: r ? { x: r.x, y: r.y, w: r.width, h: r.height } : null };
  }, [pt.x, pt.y]).catch(e => ({ error: String(e) }));
}

async function readInputLogs() {
  const top = await page.evaluate(() => window.__bwInputLog || null).catch(() => null);
  let runtime = null;
  const frame = await runtimeFrame();
  if (frame) runtime = await frame.evaluate(() => window.__bwInputLog || null).catch(() => null);
  const slim = log => log ? { count: log.length, mouse: log.filter(e => e.t.startsWith('mouse') || e.t === 'click').slice(-8), keys: log.filter(e => e.t.startsWith('key')).slice(-8) } : null;
  return { top: slim(top), runtime: slim(runtime) };
}

async function main() {
  await mkdir(outDir, { recursive: true });
  await writeFile(metricsPath, '');
  await waitForBuildIdle();
  slot = await acquireProbeSlot();
  console.log('SLOT ACQUIRED t=0');
  build = JSON.parse(await readFile(join(repo, 'dist/runtime/BUILD_INFO.json'), 'utf8'));
  server = await serve(process.env.BW_TEST_DIST || 'dist');
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
    headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
  });
  page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  page.on('console', m => {
    const text = m.text();
    consoleLines.push(`[${elapsed().toFixed(1)}s] ${m.type()}: ${text}`);
    if (m.type() === 'error') consoleErrors++;
    if (text.includes('bw64: METRICS ')) {
      try {
        const json = text.slice(text.indexOf('bw64: METRICS ') + 14).split(/\r?\n/, 1)[0];
        const snap = JSON.parse(json);
        if (snap?.present?.count !== undefined) lastWasmPresent = snap.present.count;
      } catch {}
    }
  });
  page.on('pageerror', e => { pageErrors++; consoleLines.push(`[${elapsed().toFixed(1)}s] pageerror: ${e.message}`); });
  page.on('response', r => { if (r.status() >= 400) consoleLines.push(`[${elapsed().toFixed(1)}s] http-error: ${r.status()} ${r.url()}`); });

  const baseUrl = `http://127.0.0.1:${server.address().port}/`;
  await page.goto(baseUrl, { waitUntil: 'domcontentloaded' });
  await applyJitSetting(page, '1');
  const t0 = Date.now();
  await launchDemoGame(page, 'portal', { mappedTimeout, onClick: () => {} });
  const tMapped = elapsed();
  await recordStep('mapped', { tMappedSeconds: tMapped });
  startSampler();
  const loggersInstalled = await installInputLoggers();
  await recordStep('input-loggers', { installed: loggersInstalled });

  // --- Phase 1: wait for the LIVE main menu ---
  // Portal first presents a blurred "Loading..." backdrop frame, then spends
  // minutes loading the menu background (presenting almost nothing), then the
  // full menu streams at ~2fps with ~77k distinct colors (calibration:
  // portal-d24-fix/after2 metrics + screenshots). The backdrop frame alone
  // (~39k colors) must NOT be mistaken for the menu.
  let menuBuf = null, menuAt = null;
  const menuDeadline = Date.now() + menuWaitS * 1000;
  const menuHistory = [];
  while (Date.now() < menuDeadline && Date.now() < overallDeadline) {
    await page.waitForTimeout(15000);
    await flushConsole();
    const buf = await clipShot(null);
    const st = await pixelStatsOf(buf);
    const m = await sampleMetrics();
    const frames = m.pageTier?.received?.frames ?? null;
    menuHistory.push({ t: elapsed(), frames, colors: st.colors });
    const prev = menuHistory.at(-2);
    const framesDelta = (frames !== null && prev?.frames !== null && prev) ? frames - prev.frames : null;
    const richColors = Number(st.colors) > 55000;
    const streaming = framesDelta !== null && framesDelta >= 8 && Number(st.colors) > 45000;
    console.log(`MENU-POLL t=${elapsed()}s frames=${frames} dFrames=${framesDelta} colors=${st.colors} rich=${richColors} streaming=${streaming}`);
    if (richColors || streaming) {
      // Confirm with one more sample a few seconds later.
      await page.waitForTimeout(6000);
      const buf2 = await clipShot(null);
      const st2 = await pixelStatsOf(buf2);
      if (Number(st2.colors) > 50000) { menuBuf = buf2; menuAt = elapsed(); break; }
    }
    menuBuf = buf;
  }
  if (!menuAt) { await recordStep('menu-timeout', { menuHistory: menuHistory.slice(-6) }); throw new Error('live menu never detected'); }
  await writeFile(join(outDir, 'menu.png'), menuBuf);
  await fullShot('menu-full.png');
  const menuMetrics = await sampleMetrics();
  await recordStep('menu-live', { menuAtSeconds: menuAt, receivedFrames: menuMetrics.pageTier?.received?.frames ?? null, menuHistory: menuHistory.slice(-4) });

  // --- Phase 2a: fresh baseline + coordinator-supplied NEW GAME point ---
  // (The menu background camera orbits continuously, so frames never
  // "settle"; baselines must be seconds old, not minutes.)
  async function waitForCoords(name, timeoutS, fallback) {
    const path = join(outDir, name);
    const deadline = Date.now() + timeoutS * 1000;
    while (Date.now() < deadline && Date.now() < overallDeadline) {
      try {
        const parsed = JSON.parse(await readFile(path, 'utf8'));
        await recordStep(`coords-${name}`, { parsed });
        return parsed;
      } catch { await page.waitForTimeout(10000); }
    }
    await recordStep(`coords-${name}-timeout`, { fallback });
    return fallback;
  }

  const [dcx, dcy] = await (async () => { const c = await displayClip(); return [c.x + c.width / 2, c.y + c.height / 2]; })();
  await page.mouse.click(dcx, dcy); // focus first, as demo-matrix does pre-input
  await page.waitForTimeout(1200);
  const coords1 = await waitForCoords('coords-ng.json', Number(process.env.COORDS_WAIT_S || 900), { ng: NG });
  const ngPoint = coords1.ng || NG;
  let settleBuf = await clipShot('menu-baseline.png');
  await page.mouse.move(ngPoint.x, ngPoint.y, { steps: 8 });
  await page.waitForTimeout(1500);
  await clipShot('hover-ng.png');
  await recordStep('hover-probe', { point: ngPoint });

  // --- Phase 2b: NEW GAME hold-click with DOM input instrumentation.
  // The element stack at the click point and the per-document event logs
  // (launcher top page vs runtime iframe) locate the input path exactly.
  // The coordinator verifies from step-01/step-02 whether the dialog
  // opened and answers via coords-chst.json:
  // {ch, st} | {keyboardTest: true} | {abort: true}. ---
  const clickPoint = ngPoint;
  await recordStep('event-stack', await probeEventStack(ngPoint));
  await recordStep('input-log-after-hover', await readInputLogs());
  await clickAt(ngPoint, 'new-game-hold');
  await page.waitForTimeout(4000);
  let dialogBuf = await clipShot('step-01-newgame-attempt.png');
  await fullShot('step-01-newgame-full.png');
  await recordStep('newgame-hold-click', { point: ngPoint, diffVsBaseline: diffStats(settleBuf, dialogBuf), inputLogs: await readInputLogs() });
  await page.waitForTimeout(2000);
  dialogBuf = await clipShot('step-02-dialog-settled.png');
  await recordStep('dialog-check', { stats: await pixelStatsOf(dialogBuf) });

  let coords2 = await waitForCoords('coords-chst.json', Number(process.env.COORDS_WAIT_S || 900), { ch: CH, st: ST });
  if (coords2.abort) {
    await recordStep('coordinator-abort', { note: 'dialog did not open; input path blocked at the menu' });
    throw new Error('coordinator abort: NEW GAME dialog did not open');
  }
  if (coords2.keyboardTest) {
    // Keyboard battery: does the menu respond to keys at all?
    await page.keyboard.press('Enter');
    await page.waitForTimeout(3000);
    await clipShot('kb-01-enter.png');
    await page.keyboard.press('ArrowDown');
    await page.waitForTimeout(600);
    await page.keyboard.press('ArrowDown');
    await page.waitForTimeout(600);
    await page.keyboard.press('Enter');
    await page.waitForTimeout(3000);
    await clipShot('kb-02-arrows-enter.png');
    await clickAt(clickPoint, 'new-game-hold-2500', 2500);
    await page.waitForTimeout(4000);
    await clipShot('kb-03-long-hold-click.png');
    await fullShot('kb-03-long-hold-full.png');
    await recordStep('keyboard-battery', { note: 'Enter; Down Down Enter; 2500ms hold click; shots kb-01..03' });
    coords2 = await waitForCoords('coords-final.json', Number(process.env.COORDS_WAIT_S || 900), { abort: true });
    if (coords2.abort || !coords2.ch || !coords2.st) {
      await recordStep('coordinator-abort', { note: 'no dialog after mouse+keyboard battery' });
      throw new Error('coordinator abort: no dialog after input battery');
    }
  }
  const chPoint = coords2.ch || CH;
  const stPoint = coords2.st || ST;
  dialogBuf = await clipShot('step-02b-dialog-confirmed.png');
  await clickAt(chPoint, 'chapter');
  await page.waitForTimeout(2000);
  const chBuf = await clipShot('step-03-chapter-click.png');
  await recordStep('chapter-click', { point: chPoint, stats: await pixelStatsOf(chBuf), diffVsDialog: diffStats(dialogBuf, chBuf) });
  // From here the start retry in the load watch must use the same point.
  ST.x = stPoint.x; ST.y = stPoint.y;
  await clickAt(stPoint, 'start-game');
  await page.waitForTimeout(3000);
  const startBuf = await clipShot('step-04-start-click.png');
  const startMetrics = await sampleMetrics();
  await recordStep('start-click', {
    point: stPoint,
    stats: await pixelStatsOf(startBuf), diffVsChapter: diffStats(chBuf, startBuf),
    receivedFrames: startMetrics.pageTier?.received?.frames ?? null,
  });

  // --- Phase 3: level-load watch ---
  let chamberAt = null, chamberBuf = null, prevLoadBuf = startBuf, loadIndex = 0;
  let refBuf = startBuf, retriedStart = false;
  const loadDeadline = Date.now() + loadWaitS * 1000;
  let lastPresents = null, stalledSamples = 0;
  while (Date.now() < loadDeadline && Date.now() < overallDeadline) {
    await page.waitForTimeout(30000);
    loadIndex++;
    const buf = await clipShot(`load-${String(loadIndex).padStart(2, '0')}.png`);
    const m = await sampleMetrics();
    const presents = m.pageTier?.received?.frames ?? null;
    const dVsStart = diffStats(refBuf, buf);
    const dVsPrev = diffStats(prevLoadBuf, buf);
    if (presents !== null && lastPresents !== null) {
      if (presents === lastPresents) stalledSamples++; else stalledSamples = 0;
    }
    if (presents !== null) lastPresents = presents;
    await recordStep(`load-watch-${loadIndex}`, {
      receivedFrames: presents, stalledSamples,
      diffVsStart: dVsStart.changedFraction, diffVsPrev: dVsPrev.changedFraction,
    });
    prevLoadBuf = buf;
    // If 150s after the start click the frame is still the dialog, the start
    // click likely missed: retry once at a small offset and re-baseline.
    if (!retriedStart && loadIndex === 5 && dVsStart.changedFraction < 0.02) {
      retriedStart = true;
      await recordStep('start-retry', { note: 'frame unchanged 150s after start click; re-clicking start offset' });
      await clickAt({ x: ST.x - 40, y: ST.y - 12 }, 'start-game-retry');
      await page.waitForTimeout(3000);
      refBuf = await clipShot('step-05-start-retry.png');
      prevLoadBuf = refBuf;
      continue;
    }
    const dVsMenu = diffStats(settleBuf, buf);
    if (dVsStart.changedFraction > 0.25 && dVsMenu.changedFraction > 0.3) {
      // Scene replaced the dialog AND differs wholesale from the menu view:
      // confirm it is a live scene, not a static loading card, with one
      // more sample.
      await page.waitForTimeout(20000);
      const confirm = await clipShot('load-confirm.png');
      const dConfirm = diffStats(buf, confirm);
      await recordStep('chamber-candidate', { diffVsMenu: dVsMenu.changedFraction, diffBetweenSamples: dConfirm.changedFraction });
      chamberBuf = confirm; chamberAt = elapsed();
      break;
    }
  }
  if (!chamberAt) await recordStep('load-timeout', { note: 'scene never replaced the post-start frame' });
  else {
    await recordStep('chamber-live', { chamberAtSeconds: chamberAt });
    await fullShot('chamber-full.png');
  }

  // --- Phase 4: input proof in whatever scene we are in ---
  const [cx, cy] = await (async () => { const c = await displayClip(); return [c.x + c.width / 2, c.y + c.height / 2]; })();
  await page.mouse.click(cx, cy); // focus / pointer capture, as in demo-matrix
  await page.waitForTimeout(1500);

  const inputPairs = [];
  async function inputPair(label, action, settleMs = 2500) {
    const before = await clipShot(`input-${label}-before.png`);
    await action();
    await page.waitForTimeout(settleMs);
    const after = await clipShot(`input-${label}-after.png`);
    const d = diffStats(before, after);
    inputPairs.push({ label, ...d });
    await recordStep(`input-${label}`, d);
    return d;
  }

  await inputPair('look-right', () => page.mouse.move(cx + 280, cy, { steps: 14 }));
  await inputPair('look-left-back', () => page.mouse.move(cx - 280, cy, { steps: 14 }));
  await inputPair('forward-w', () => holdKey('w', 3000), 2000);
  await inputPair('strafe-a', () => holdKey('a', 2000), 2000);
  await inputPair('jump-space', () => page.keyboard.press(' '), 2000);
  await inputPair('back-s', () => holdKey('s', 2000), 2000);

  // --- Phase 5: free-run (portal opening / continued play) + FPS window ---
  const freeStart = await sampleMetrics();
  const freeStartFrames = freeStart.pageTier?.received?.frames ?? null;
  const freeStartAt = elapsed();
  let freeIndex = 0;
  const freeDeadline = Date.now() + freerunS * 1000;
  while (Date.now() < freeDeadline && Date.now() < overallDeadline) {
    await page.waitForTimeout(30000);
    freeIndex++;
    await clipShot(`freerun-${String(freeIndex).padStart(2, '0')}.png`);
  }
  // One last push forward (through the vault portal if it has opened).
  await inputPair('final-forward', () => holdKey('w', 4000), 2500);
  const freeEnd = await sampleMetrics();
  const freeEndFrames = freeEnd.pageTier?.received?.frames ?? null;
  const freeEndAt = elapsed();
  const chamberFps = (freeStartFrames !== null && freeEndFrames !== null && freeEndAt > freeStartAt)
    ? Number(((freeEndFrames - freeStartFrames) / (freeEndAt - freeStartAt)).toFixed(2)) : null;
  await recordStep('freerun', { fromSeconds: freeStartAt, toSeconds: freeEndAt, framesStart: freeStartFrames, framesEnd: freeEndFrames, fpsPresented: chamberFps });

  sampling = false;
  await samplerPromise?.catch(() => {});
  const logText = await page.locator('#log').textContent().catch(() => '') || '';
  await writeFile(join(outDir, 'log.txt'), logText);
  const summary = {
    demo: 'portal', jit: 1, build, outDir,
    tMappedSeconds: tMapped, menuAtSeconds: menuAt, chamberAtSeconds: chamberAt,
    chamberFpsPresented: chamberFps,
    clicks: { newGame: NG, chapter: CH, start: ST },
    steps, inputPairs,
    pageErrorCount: pageErrors, consoleErrorCount: consoleErrors,
    totalSeconds: elapsed(),
  };
  await writeFile(join(outDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
  console.log('SUMMARY', JSON.stringify({ menuAt, chamberAt, chamberFps, inputPairs, totalSeconds: elapsed() }));
}

try {
  await main();
  console.log('PROBE DONE');
} catch (error) {
  console.error('PROBE FAILED:', error?.stack || error);
  try {
    if (page) {
      await fullShot('failure.png').catch(() => {});
      const logText = await page.locator('#log').textContent().catch(() => '') || '';
      await writeFile(join(outDir, 'log.txt'), logText).catch(() => {});
    }
    await writeFile(join(outDir, 'summary.json'), JSON.stringify({
      demo: 'portal', failed: String(error?.message || error), steps,
      pageErrorCount: pageErrors, consoleErrorCount: consoleErrors, totalSeconds: elapsed(),
    }, null, 2) + '\n').catch(() => {});
  } catch {}
  process.exitCode = 1;
} finally {
  sampling = false;
  await samplerPromise?.catch(() => {});
  if (page) await writeFile(join(outDir, 'console.txt'), consoleLines.join('\n') + '\n').catch(() => {});
  await browser?.close().catch(() => {});
  if (server) await new Promise(r => server.close(r)).catch(() => {});
  await slot?.release().catch(() => {});
}
