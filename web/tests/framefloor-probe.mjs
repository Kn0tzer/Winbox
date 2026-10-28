// Frame-floor probe: captures per-present timing evidence for a demo run
// through the real launcher. Data collected:
//   - every console line with a host receive timestamp (mined offline)
//   - bw64 METRICS lines -> per-window present counts with timestamps
//   - an in-runtime-frame canvas-change sampler (hash of a downscaled copy,
//     ~40 ms period) -> frame-change timestamps for non-VK paths
// Env: DEMO (portal|doom), DIST_DIR (absolute, served as-is), RECORD_S,
//      SETTLE_S, JIT (default 1), OUT (dir), MAPPED_TIMEOUT_S.
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { threadDumpDriverLine, threadDumpStatus } from './thread-dump-driver.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { applyJitSetting } from './demo-matrix.mjs';
import { createGuestDeathWatch, extractMetricsPresent } from './probe-guest-death.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { appendFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const demo = process.env.DEMO;
if (!demo) throw new Error('DEMO required');
const distDir = process.env.DIST_DIR;
if (!distDir) throw new Error('DIST_DIR required (absolute path)');
const recordS = Number(process.env.RECORD_S || 90);
const settleS = Number(process.env.SETTLE_S || 20);
const jit = process.env.JIT || '1';
const mappedTimeout = Number(process.env.MAPPED_TIMEOUT_S || (demo === 'portal' ? 900 : 300));
const outDir = resolve(process.env.OUT || `test-results/framefloor/${demo}-${Date.now()}`);
await mkdir(outDir, { recursive: true });
// Defect 4: node stdout to a redirected file stayed empty until probe exit.
// Force blocking stdout (when available) AND mirror milestones synchronously
// to a progress file so a watcher sees them during the run.
try { process.stdout._handle?.setBlocking?.(true); } catch {}
const progressPath = outDir + '.progress.log';
try { writeFileSync(progressPath, ''); } catch {}

const loadavg = async () => { try { return (await readFile('/proc/loadavg', 'utf8')).trim(); } catch { return '?'; } };
console.log('LOAD at start:', await loadavg());

if (process.env.SKIP_BUILD_WAIT !== '1') await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
const server = await serve(distDir);
const baseUrl = `http://127.0.0.1:${server.address().port}/`;
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
if (process.env.THREAD_DUMPS === '1') {
  await page.route('**/wine64-launcher.js*', async (route) => {
    const response = await route.fetch();
    const body = await response.text();
    const marker = 'if (param("unimpldump") === "1") ENV["BW64_UNIMPLDUMP"] = "1";';
    if (!body.includes(marker)) { console.log('DUMP PATCH: marker missing, passing through'); return route.fulfill({ response, body }); }
    const instrumented = body.replace(marker, 'if (param("bw64diag") === "threads") { ENV["BW_THREAD_DUMP"] = "1"; }\n                ' + marker);
    console.log('DUMP PATCH: launcher instrumented');
    return route.fulfill({ response, body: instrumented });
  });
}
const t0 = Date.now();
const now = () => Date.now() - t0;
const progress = (msg) => {
  const line = `[${(now() / 1000).toFixed(1)}s] ${msg}`;
  console.log(line);
  try { appendFileSync(progressPath, line + '\n'); } catch {}
};
const consoleLines = [];
const metricsLines = [];
const pageErrors = [];
const streamPath = outDir + '.console.jsonl';
try { writeFileSync(streamPath, ''); } catch {}
// Defect 2: guest-death detection (see probe-guest-death.mjs). A dead guest
// must end the probe with GUEST-DIED, never burn a full mapping/activity
// timeout the way Doom A (~130s) and Gang Beasts (~50 min) did.
const deathWatch = createGuestDeathWatch();
let guestDeath = null;
let metricsTruncatedLines = 0;
let sdlPresentsTotal = 0;
const noteDeath = () => {
  const d = deathWatch.check(now());
  if (d && !guestDeath) {
    guestDeath = d;
    progress(`GUEST-DIED kind=${d.kind} atMs=${d.atMs} sig=${String(d.signature).slice(0, 160)}`);
  }
  return guestDeath;
};
// Defect 1: the console METRICS channel truncates at ~832 payload chars in
// play phase, so full snapshots are unparseable there. Poll the wasm census
// directly (bw64_jit_stats_json_text via ccall, the channel demo-matrix
// already uses) instead of trusting the console line.
const jitStatsSamples = [];
let jitStatsPollErrors = 0;
let jitPoll = null;
const getRuntimeFrame = async () => {
  try { const fh = await page.locator('#display iframe').elementHandle(); return await fh?.contentFrame(); } catch { return null; }
};
const pollJitStats = async () => {
  try {
    const frame = await getRuntimeFrame();
    if (!frame) return;
    const stats = await frame.evaluate(() => {
      if (typeof Module === 'undefined' || typeof Module.ccall !== 'function') throw new Error('runtime Module export is not available');
      const r = Module.ccall('bw64_jit_stats_json_text', 'string', [], []);
      if (typeof r === 'string') return JSON.parse(r);
      if (r && typeof Module.UTF8ToString === 'function') return JSON.parse(Module.UTF8ToString(r));
      throw new Error('bw64_jit_stats_json_text returned no readable text');
    });
    jitStatsSamples.push({ t: now(), stats });
  } catch { jitStatsPollErrors++; }
};
page.on('console', m => {
  const text = m.text();
  const entry = { t: now(), type: m.type(), text };
  consoleLines.push(entry);
  deathWatch.feed(entry);
  try { appendFileSync(streamPath, JSON.stringify(entry) + '\n'); } catch {}
  const marker = 'bw64: METRICS ';
  const at = text.indexOf(marker);
  if (at >= 0) {
    // Defect 3: the present object survives console truncation even when the
    // full line does not parse, so SDL presents count here (see below).
    const present = extractMetricsPresent(text);
    if (present) sdlPresentsTotal += present.sdl;
    try {
      const snap = JSON.parse(text.slice(at + marker.length).split(/\r?\n/, 1)[0].trim());
      metricsLines.push({ t: now(), present: snap?.present?.count ?? null, snap });
    } catch { metricsTruncatedLines++; }
  }
  noteDeath();
});
page.on('pageerror', e => {
  const msg = String(e && e.message || e);
  pageErrors.push({ t: now(), msg });
  deathWatch.feedPageError(msg, now());
  noteDeath();
});

const urlParams = new URLSearchParams(process.env.URL_PARAMS || '');
if ([...urlParams].length) {
  await page.addInitScript(params => {
    const descriptor = Object.getOwnPropertyDescriptor(HTMLIFrameElement.prototype, 'src');
    if (!descriptor?.set || !descriptor?.get) throw new Error('Cannot instrument runtime iframe src');
    Object.defineProperty(HTMLIFrameElement.prototype, 'src', {
      configurable: true, enumerable: descriptor.enumerable,
      get() { return descriptor.get.call(this); },
      set(value) {
        const url = new URL(value, document.baseURI);
        if (url.pathname.endsWith('/runtime/index.html')) {
          for (const [k, v] of params) url.searchParams.set(k, v);
        }
        descriptor.set.call(this, url.toString());
      },
    });
  }, [...urlParams]);
}
await page.goto(baseUrl, { waitUntil: 'domcontentloaded' });
await applyJitSetting(page, jit);
const clickAt = now();
let launchError = null;
try {
  await page.locator(`[data-game="${demo}"]`).click();
  // Death-aware mapping wait: poll the launcher's 'first window mapped'
  // marker in 1s slices (the same condition launchDemoGame waits on) so a
  // dead guest aborts promptly instead of burning mappedTimeout.
  const deadline = Date.now() + mappedTimeout * 1000;
  let mapped = false;
  while (Date.now() < deadline) {
    if (noteDeath()) break;
    try { mapped = await page.evaluate(() => document.getElementById('log')?.textContent.includes('first window mapped')); } catch {}
    if (mapped) break;
    await page.waitForTimeout(1000);
  }
  if (!mapped && !guestDeath) throw new Error(`page.waitForFunction: Timeout ${mappedTimeout * 1000}ms exceeded.`);
} catch (e) { launchError = String(e && e.message || e); progress('LAUNCH FAILED: ' + launchError); }
const mappedAt = (launchError || guestDeath) ? null : now();
if (!launchError && !guestDeath) progress(`MAPPED demo=${demo} clickToMappedMs=${mappedAt - clickAt}`);

let recordStart = now();
let recordEnd = now();
if (!launchError && !guestDeath) {
// Install the canvas-change sampler inside the runtime iframe.
let samplerInstalled = false;
try {
  const frameHandle = await page.locator('#display iframe').elementHandle();
  const frame = await frameHandle?.contentFrame();
  if (frame) {
    await frame.evaluate(() => {
      const cv = document.querySelector('canvas');
      if (!cv) { window.__ff = { error: 'no canvas', changes: [] }; return; }
      const off = document.createElement('canvas');
      off.width = 64; off.height = 36;
      const ctx = off.getContext('2d', { willReadFrequently: true });
      let last = -1;
      const changes = [];
      const iv = setInterval(() => {
        try {
          ctx.drawImage(cv, 0, 0, 64, 36);
          const d = ctx.getImageData(0, 0, 64, 36).data;
          let h = 0;
          for (let i = 0; i < d.length; i += 16) h = ((h * 31) ^ d[i]) >>> 0;
          if (h !== last) { last = h; changes.push(performance.now()); }
        } catch (e) { window.__ffError = String(e && e.message || e); clearInterval(iv); }
      }, 40);
      window.__ff = { changes, canvasW: cv.width, canvasH: cv.height };
      // rAF cadence inside the runtime frame: the emscripten main loop
      // (which pumps X11 presents, timers and main-thread hops) is driven
      // by this clock, so its rate is the ceiling for those paths.
      const rafTimes = [];
      const rafTick = (t) => { if (rafTimes.length < 20000) rafTimes.push(t); requestAnimationFrame(rafTick); };
      requestAnimationFrame(rafTick);
      window.__rafTimes = rafTimes;
    });
    samplerInstalled = true;
  }
} catch (e) { console.log('SAMPLER install failed:', e.message); }
progress('SAMPLER installed: ' + samplerInstalled);
await pollJitStats();
jitPoll = setInterval(() => { pollJitStats().catch(() => {}); }, 15000);
if (process.env.THREAD_DUMPS === '1') {
  const dumpEveryS = Number(process.env.DUMP_INTERVAL_S || 20);
  let dumpAttempts = 0;
  const driver = setInterval(async () => {
    try {
      const fh = await page.locator('#display iframe').elementHandle();
      const fr = await fh?.contentFrame();
      if (!fr) return;
      const dumpResult = await fr.evaluate(() => {
        const M = window.Module;
        if (!M) return null;
        if (typeof M._bw64_dump_guest_threads === 'function') return { via: 'export', rc: M._bw64_dump_guest_threads() };
        if (typeof M.ccall === 'function') return { via: 'ccall', rc: M.ccall('bw64_dump_guest_threads', 'number', [], []) };
        return null;
      });
      dumpAttempts++;
      if (dumpResult && threadDumpStatus(dumpResult.rc) === 'unavailable-lock-held') {
        progress(threadDumpDriverLine(dumpResult.rc, dumpResult.via, dumpAttempts));
      }
    } catch { /* frame churn */ }
  }, dumpEveryS * 1000);
  driver.unref?.();
}

// Wait until guest presents are actually flowing (menu reached), so the
// record window covers steady state rather than asset loading. Present
// evidence: bridge present lines in the console stream.
const waitActivityS = Number(process.env.WAIT_ACTIVITY_S || 900);
{
  const deadline = Date.now() + waitActivityS * 1000;
  let presents = 0;
  while (Date.now() < deadline) {
    // Defect 3: DIB games never emit vkQueuePresentKHR/XWire lines; their
    // presents arrive as METRICS `sdl` counts (extracted above, even from
    // truncated lines). Without this the gate burns the whole wait.
    const vkLines = consoleLines.filter(l => l.text.includes('vkQueuePresentKHR') || l.text.includes('XWire SDL: presented')).length;
    presents = vkLines + sdlPresentsTotal;
    if (presents >= 30) break;
    if (noteDeath()) break;
    // Stall backstop on GUEST activity, not raw console traffic: the
    // emulator `persist` heartbeat fires every ~5s forever, so a raw
    // console-silence check can never fire after death.
    const st = deathWatch.state();
    if (st.lastGuestActivityMs !== null && now() - st.lastGuestActivityMs > 180000) { progress('STALL: no guest activity >180s, proceeding'); break; }
    await page.waitForTimeout(5000);
  }
  progress('ACTIVITY presents=' + presents + ' (vkLines+sdl) waitedMs=' + (now() - mappedAt) + (guestDeath ? ' GUEST-DIED' : ''));
}
progress(`SETTLING ${settleS}s`);
for (let i = 0; i < settleS && !noteDeath(); i++) await page.waitForTimeout(1000);
recordStart = now();
progress(`RECORDING ${recordS}s`);
for (let i = 0; i < recordS && !noteDeath(); i++) await page.waitForTimeout(1000);
recordEnd = now();
}

let ffData = null;
try {
  const frameHandle = await page.locator('#display iframe').elementHandle();
  const frame = await frameHandle?.contentFrame();
  if (frame) ffData = await frame.evaluate(() => window.__ff ? { changes: window.__ff.changes, canvasW: window.__ff.canvasW, canvasH: window.__ff.canvasH, error: window.__ffError || window.__ff.error || null, rafTimes: (window.__rafTimes || []).slice() } : null);
  if (!ffData || !(ffData.changes || []).length) {
    for (const f of page.frames()) {
      try {
        const cand = await f.evaluate(() => window.__ff ? { changes: window.__ff.changes, canvasW: window.__ff.canvasW, canvasH: window.__ff.canvasH, error: null, rafTimes: (window.__rafTimes || []).slice() } : null);
        if (cand && ((cand.changes || []).length + (cand.rafTimes || []).length > (ffData?.changes || []).length + (ffData?.rafTimes || []).length)) ffData = cand;
      } catch { /* frame gone */ }
    }
  }
} catch (e) { console.log('SAMPLER read failed:', e.message); }

if (jitPoll) { clearInterval(jitPoll); await pollJitStats(); }
const pageTier = await page.evaluate(() => { try { return window.bwVkTier?.metrics?.() || null; } catch { return null; } }).catch(() => null);

const result = {
  demo, jit, distDir, launchError, mappedAtMs: mappedAt, recordStartMs: recordStart, recordEndMs: recordEnd,
  loadAtEnd: await loadavg(), pageErrors,
  consoleLines, metricsLines, canvasChanges: ffData, pageTier,
  guestDeath, metricsTruncatedLines, sdlPresentsTotal, jitStatsSamples, jitStatsPollErrors,
};
await writeFile(resolve(outDir, 'probe.json'), JSON.stringify(result, null, 1));
progress('WROTE ' + resolve(outDir, 'probe.json') + ' console lines: ' + consoleLines.length + ' metrics lines: ' + metricsLines.length + ' canvas changes: ' + (ffData?.changes?.length ?? null) + ' guestDeath: ' + (guestDeath ? guestDeath.kind : 'none') + ' truncatedMetrics: ' + metricsTruncatedLines + ' sdlPresents: ' + sdlPresentsTotal + ' jitSamples: ' + jitStatsSamples.length);
await browser.close();
server.close();
slot.release();
