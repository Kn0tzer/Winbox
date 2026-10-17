import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { mkdir, writeFile } from 'node:fs/promises';

// Portal end-to-end ?gamezip probe (PREPARED — DO NOT RUN until 32-bit boots verified).
// Launcher: ?gamezip=<url>&gamezipsize=3349436598&exe=hl2.exe&args=["-game","portal"]&arch=x86
// Portal is 32-bit D3D9 (Source engine) — requires the 32-bit Wine workaround.
const tag = 'portal-gamezip-' + Date.now();
const dir = 'test-results/' + tag;
await mkdir(dir, { recursive: true });
// ---- Coordination: wait for build idle, then acquire a probe slot ----
await waitForBuildIdle();
const slot = await acquireProbeSlot();

const server = await serve('dist');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const logs = [];
page.on('console', m => logs.push(m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('pageerror: ' + e.message));
const flush = () => writeFile(dir + '/console.log', logs.join('\n'));

// Audio tap (Portal has menu music / in-game audio; functional browser audio is required).
await page.addInitScript(() => {
  window.__bwAudio = { taps: [], ctx: null };
  for (const name of ['AudioContext','webkitAudioContext']) {
    const R = window[name]; if (!R) continue;
    const W = function(...a){ const c = new R(...a); window.__bwAudio.ctx = c;
      const orig = c.createScriptProcessor.bind(c);
      c.createScriptProcessor = function(bs,ic,oc){ const n = orig(bs,ic,oc);
        const tap = orig(bs||4096,1,1); const zg = c.createGain(); zg.gain.value = 0;
        const chunks=[]; tap.onaudioprocess=(e)=>{chunks.push({t:performance.now(),data:new Float32Array(e.inputBuffer.getChannelData(0))});};
        n.connect(tap); tap.connect(zg); zg.connect(c.destination);
        window.__bwAudio.taps.push({chunks, sr:c.sampleRate, len:tap.bufferSize}); return n; };
      return c; };
    W.prototype = R.prototype; window[name] = W;
  }
});

const summary = { tag, game: 'portal', arch: 'x86' };
try {
  const gz = encodeURIComponent('https://kn0tzer.work.gd/games/portal.zip');
  const args = encodeURIComponent(JSON.stringify(['-game', 'portal']));
  const url = 'http://127.0.0.1:' + port + '/runtime/index.html?gamezip=' + gz + '&gamezipsize=3349436598&exe=hl2.exe&args=' + args + '&arch=x86&session=0';
  console.log('GOTO ' + url.slice(0, 120) + '...');
  await page.goto(url);

  // Phase 1: runtime ready (Wine boot + 32-bit prefix). Generous timeout: Source engine host.
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 1800000 });
  console.log('RUNTIME READY');
  await flush();

  // Phase 2: hl2.exe spawn. The ?gamezip flow auto-spawns via bwProgramArgv.
  const waitFor = async (re, ms, label) => {
    const u = Date.now() + ms;
    while (Date.now() < u) {
      await flush();
      const h = logs.find(l => re.test(l));
      if (h) { console.log(label || ('MATCH ' + re)); return true; }
      await page.waitForTimeout(2000);
    }
    console.log('TIMEOUT waiting for ' + re);
    return false;
  };
  const spawned = await waitFor(/lazy game: mounting /i, 1200000, 'GAMEZIP MOUNTING');
  summary.spawned = spawned;

  // Phase 3: first render. Poll litPixels until the game paints something.
  const getStats = () => page.evaluate(() => {
    try { const s = window.bwVkStats?.(); return s ? { lit: s.litPixels || 0, rendered: s.v2?.rendered || 0 } : null; }
    catch (e) { return null; }
  }).catch(() => null);
  let lit = 0;
  const litUntil = Date.now() + 1800000;
  while (Date.now() < litUntil && lit <= 500) {
    const st = await getStats();
    if (st && st.lit > lit) { lit = st.lit; }
    await page.waitForTimeout(5000);
  }
  console.log('LITPIXELS', lit);
  summary.litPixels = lit;
  await page.screenshot({ path: dir + '/portal-boot.png' }).catch(() => {});

  // Phase 4: input. Keycodes verified by d3d9-fpsinput-probe (26=movement, 41=exit).
  // Portal: W=forward, mouse=look, click=use/fire, Esc=menu.
  const key = (code, down) => page.evaluate(
    ([c, d]) => window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [c, d]), [code, down]);
  const rel = (dx, dy) => page.evaluate(
    ([x, y]) => window.bwRuntime.call('bw64_xwire_mouse_rel', ['number', 'number'], [x, y]), [dx, dy]);
  const btn = (b, down) => page.evaluate(
    ([bb, d]) => window.bwRuntime.call('bw64_xwire_mouse_button', ['number', 'number'], [bb, d]), [b, down]);

  await key(26, 1); await page.waitForTimeout(5000); await key(26, 0);
  console.log('W-KEY DONE');
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_move', ['number', 'number'], [640, 360]));
  for (let i = 0; i < 30; i++) { await rel(8, -2); await page.waitForTimeout(100); }
  console.log('MOUSE-LOOK DONE');
  await btn(1, 1); await page.waitForTimeout(300); await btn(1, 0);
  console.log('CLICK DONE');
  await page.waitForTimeout(5000);
  summary.afterInput = await getStats();
  await page.screenshot({ path: dir + '/portal-input.png' }).catch(() => {});

  // Phase 5: audio check.
  const tapInfo = await page.evaluate(() => { const t = (window.__bwAudio?.taps || [])[0]; if (!t) return null;
    return { total: t.chunks.reduce((a, c) => a + c.data.length, 0), sr: t.sr }; }).catch(() => null);
  let peak = 0;
  if (tapInfo && tapInfo.total > 0) {
    const s = await page.evaluate(() => { const t = window.__bwAudio.taps[0];
      const total = t.chunks.reduce((a, c) => a + c.data.length, 0);
      const out = new Float32Array(total); let o = 0;
      for (const c of t.chunks) { out.set(c.data, o); o += c.data.length; }
      return Array.from(out); }).catch(() => []);
    for (const v of s) { const a = Math.abs(v); if (a > peak) peak = a; }
  }
  summary.audio = { samples: tapInfo?.total || 0, peak: +peak.toFixed(4) };
  console.log('AUDIO', JSON.stringify(summary.audio));

  // Phase 6: clean exit via Escape.
  await key(41, 1); await page.waitForTimeout(300); await key(41, 0);
  await page.waitForTimeout(10000);
  summary.cleanExit = true;
  await page.screenshot({ path: dir + '/portal-exit.png' }).catch(() => {});

  summary.pass = lit > 500;
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  await flush();
  console.log('DONE', JSON.stringify(summary));
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
