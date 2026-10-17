// Audio real-world verification probe.
// Verifies: (1) autoplay gate blocks audio before a REAL click,
// (2) real Playwright click unlocks it, (3) captured waveform is 440Hz,
// (4) end-to-end latency, (5) DirectSound playback.
// Run from repo root: TAG=unique node web/tests/audio-realworld-probe.mjs
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';

const tag = process.env.TAG || `audio-real-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/${tag}`;
await mkdir(dir, { recursive: true });
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  // NOTE: deliberately NOT passing --autoplay-policy=no-user-gesture-required.
  // We want the real autoplay gate so we can verify the click unlock.
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
const logTimes = []; // {t: Date.now(), text}
page.on('console', m => { const t = Date.now(); const txt = `${m.type()}: ${m.text()}`; logs.push(txt); logTimes.push({ t, txt }); });
page.on('pageerror', e => errors.push(e.message));
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const timeout = Number(process.env.TIMEOUT || 1800000);

// ---- AudioContext tap: installed before any page script runs ----
await page.addInitScript(() => {
  const patch = (name) => {
    const RealAC = window[name];
    if (!RealAC) return;
    window.__bwAudio = window.__bwAudio || { taps: [], ctx: null, events: [] };
    const wrapCtx = (ctx) => {
      window.__bwAudio.ctx = ctx;
      window.__bwAudio.events.push({ t: performance.now(), ev: 'ctx-created', state: ctx.state });
      ctx.addEventListener('statechange', () => {
        window.__bwAudio.events.push({ t: performance.now(), ev: 'statechange', state: ctx.state });
      });
      const orig = ctx.createScriptProcessor.bind(ctx);
      ctx.createScriptProcessor = function (bufferSize, inCh, outCh) {
        const node = orig(bufferSize, inCh, outCh);
        const tap = orig(bufferSize || 4096, 1, 1);
        const zg = ctx.createGain(); zg.gain.value = 0;
        const chunks = [];
        tap.onaudioprocess = (e) => {
          chunks.push({ t: performance.now(), data: new Float32Array(e.inputBuffer.getChannelData(0)) });
        };
        node.connect(tap);
        tap.connect(zg); zg.connect(ctx.destination);
        window.__bwAudio.taps.push({ node, tap, chunks, sampleRate: ctx.sampleRate, nodeBuf: node.bufferSize });
        window.__bwAudio.events.push({ t: performance.now(), ev: 'scriptprocessor-created', buf: node.bufferSize, sr: ctx.sampleRate });
        return node;
      };
      return ctx;
    };
    const Wrapped = function (...a) { return wrapCtx(new RealAC(...a)); };
    Wrapped.prototype = RealAC.prototype;
    window[name] = Wrapped;
  };
  patch('AudioContext');
  patch('webkitAudioContext');
});

const summary = { tag, phases: {} };
try {
  // Video mode (no novideo=1): we need the canvas for the real click.
  // p=cmd /c echo vkboot boots the Wine environment (wineserver, services) like other probes.
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  // Stage test binaries.
  for (const [name, src] of [['audiotone.exe', '/tmp/audioprobe/audiotone.exe'], ['dstone.exe', '/tmp/audioprobe/dstone.exe']]) {
    const bytes = await readFile(src);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const registered = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
      if (registered !== 1) throw new Error('Guest VFS registration failed for ' + name);
    }, { name, b64 });
  }
  console.log('STAGED');

  const bootstrapUntil = Date.now() + 120000;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) { console.log('BOOTSTRAP TIMEOUT'); break; }
    await flush();
    await page.waitForTimeout(500);
  }
  console.log('BOOTSTRAP COMPLETE');

  // Pin the wineserver with a long-running dummy so it doesn't exit between spawns.
  await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\windows\\\\system32\\\\ping.exe\n127.0.0.1\n-n\n100', 'LD_LIBRARY_PATH=/home/username']));
  await page.waitForTimeout(3000);
  console.log('WINESERVER PINNED');

  const spawnExe = async (exe) => {
    const startIdx = logs.length;
    const r = await page.evaluate((exe) => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
      ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\' + exe, 'LD_LIBRARY_PATH=/home/username']), exe);
    // extract pid from "spawned pid=N" log
    let pid = 0;
    const until = Date.now() + 10000;
    while (Date.now() < until && !pid) {
      const m = logs.slice(startIdx).find(l => /spawned pid=(\d+)/.test(l));
      if (m) pid = Number(m.match(/spawned pid=(\d+)/)[1]);
      else await page.waitForTimeout(500);
    }
    console.log('SPAWN', exe, r, 'pid=' + pid);
    return { startIdx, pid };
  };
  const killPid = (pid) => page.evaluate((pid) => {
    try { return window.bwRuntime.call('bw64_kill', ['number'], [pid]); } catch (e) { return 'err:' + e.message; }
  }, pid);
  const waitFor = async (re, ms, label) => {
    const until = Date.now() + ms;
    while (Date.now() < until) {
      await flush();
      const hit = logs.find(l => re.test(l));
      if (hit) {
        const lt = logTimes.find(x => x.txt === hit);
        return { hit, t: lt ? lt.t : Date.now() };
      }
      await page.waitForTimeout(500);
    }
    throw new Error('timeout waiting for ' + label);
  };
  const audioState = () => page.evaluate(() => ({
    ctxState: window.__bwAudio?.ctx?.state || 'none',
    taps: (window.__bwAudio?.taps || []).map(t => ({ chunks: t.chunks.length, sr: t.sampleRate })),
    events: window.__bwAudio?.events || [],
  }));
  const getTapData = () => page.evaluate(() => {
    const t = (window.__bwAudio?.taps || [])[0];
    if (!t) return null;
    const total = t.chunks.reduce((a, c) => a + c.data.length, 0);
    const out = new Float32Array(total);
    const times = new Float64Array(t.chunks.length);
    let o = 0;
    t.chunks.forEach((c, i) => { out.set(c.data, o); o += c.data.length; times[i] = c.t; });
    return { samples: Array.from(out), times: Array.from(times), sr: t.sampleRate, chunkLen: t.chunks[0]?.data.length || 0 };
  });
  const clearTap = () => page.evaluate(() => { for (const t of (window.__bwAudio?.taps || [])) t.chunks.length = 0; });

  // ---------- PHASE 1: no click -> expect silence ----------
  console.log('PHASE 1: spawn without click (expect blocked)');
  const p1 = await spawnExe('audiotone.exe');
  // Don't wait for WHDR_DONE: with soundEnabled=false the virtual buffer may
  // never mark the header done. Wait long enough for the 3.5s tone to virtually play.
  await waitFor(/audiotone: TONESTART/, 240000, 'phase1 tonestart');
  await page.waitForTimeout(15000);
  // The phase-1 process may be stuck in WHDR_DONE wait (virtual buffer never
  // completes). Kill it so it doesn't hold /dev/dsp.
  console.log('KILL phase1', await killPid(p1.pid));
  const st1 = await audioState();
  const tap1 = await getTapData();
  const silent1 = !tap1 || tap1.samples.every(s => Math.abs(s) < 0.02);
  summary.phases.noClick = { ctxState: st1.ctxState, tapChunks: tap1 ? tap1.times.length : 0, silent: silent1, events: st1.events };
  console.log('PHASE1', JSON.stringify({ ctx: st1.ctxState, chunks: tap1?.times.length || 0, silent: silent1 }));

  // ---------- PHASE 2: REAL click -> unlock ----------
  console.log('PHASE 2: real Playwright click on canvas');
  await clearTap();
  const before = await audioState();
  // Find the canvas and click its center explicitly. Also instrument to verify
  // the mousedown actually reaches the canvas element.
  const canvasInfo = await page.evaluate(() => {
    const c = document.getElementById('canvas') || document.querySelector('canvas');
    if (!c) return { found: false };
    const r = c.getBoundingClientRect();
    let gotDown = false;
    c.addEventListener('mousedown', () => { gotDown = true; window.__bwClickHit = true; }, { once: true });
    return { found: true, x: r.x + r.width / 2, y: r.y + r.height / 2, w: r.width, h: r.height };
  });
  console.log('CANVAS', JSON.stringify(canvasInfo));
  if (canvasInfo.found) {
    await page.mouse.click(canvasInfo.x, canvasInfo.y); // real trusted input on the canvas
  } else {
    await page.mouse.click(640, 450);
  }
  await page.waitForTimeout(2000);
  const clickHit = await page.evaluate(() => !!window.__bwClickHit);
  const after = await audioState();
  summary.phases.click = { before: before.ctxState, after: after.ctxState, canvasInfo, clickHit, events: after.events };
  console.log('PHASE2', JSON.stringify({ before: before.ctxState, after: after.ctxState, clickHit }));

  // ---------- PHASE 3: spawn again -> expect 440Hz ----------
  console.log('PHASE 3: spawn after click (expect 440Hz tone)');
  await clearTap();
  let ts = null;
  for (let attempt = 1; attempt <= 3 && !ts; attempt++) {
    const sp = await spawnExe('audiotone.exe');
    try {
      ts = await waitFor(/audiotone: TONESTART/, 120000, 'tonestart attempt ' + attempt);
    } catch (e) {
      console.log('RETRY spawn', attempt, 'stuck; killing pid=' + sp.pid);
      await killPid(sp.pid);
      await page.waitForTimeout(2000);
    }
  }
  if (!ts) throw new Error('phase3 spawn failed after 3 attempts');
  // Don't wait for WHDR_DONE: it's never set (winealsa doesn't mark headers done
  // in this setup). Wait for the 3.5s tone to play plus overhead, then capture.
  await page.waitForTimeout(25000);
  const tap3 = await getTapData();
  await writeFile(`${dir}/tap3.json`, JSON.stringify({ sr: tap3?.sr, n: tap3?.samples.length, times: tap3?.times }));
  // analyze in Node
  const s = Float32Array.from(tap3.samples);
  const sr = tap3.sr;
  // onset: first sustained |x|>0.05
  let onset = -1;
  for (let i = 0; i + 200 < s.length; i++) {
    let ok = true;
    for (let j = 0; j < 200; j += 10) if (Math.abs(s[i + j]) < 0.05) { ok = false; break; }
    if (ok) { onset = i; break; }
  }
  // frequency via zero crossings in a stable 1s window after onset+0.5s
  const w0 = onset + Math.floor(sr * 0.7), w1 = w0 + sr;
  let zc = 0;
  for (let i = w0 + 1; i < Math.min(w1, s.length); i++) if ((s[i - 1] < 0) !== (s[i] < 0)) zc++;
  const freq = zc / 2;
  let peak = 0, sum = 0, n2 = 0;
  for (let i = w0; i < Math.min(w1, s.length); i++) { const a = Math.abs(s[i]); if (a > peak) peak = a; sum += s[i] * s[i]; n2++; }
  const rms = Math.sqrt(sum / n2);
  // latency: onset page-time minus TONESTART page-time
  let onsetT = null;
  if (onset >= 0) {
    const ci = Math.floor(onset / tap3.chunkLen);
    onsetT = tap3.times[Math.min(ci, tap3.times.length - 1)] + (onset % tap3.chunkLen) / sr * 1000;
  }
  const latencyMs = onsetT !== null ? onsetT - ts.t : null;
  summary.phases.tone = { onset, freqHz: freq, peak, rms, latencyMs, sr, totalSamples: s.length };
  console.log('PHASE3', JSON.stringify(summary.phases.tone));

  // ---------- PHASE 4: DirectSound ----------
  console.log('PHASE 4: DirectSound');
  await clearTap();
  let dsDone = null;
  for (let attempt = 1; attempt <= 3 && !dsDone; attempt++) {
    const sp = await spawnExe('dstone.exe');
    try {
      dsDone = await waitFor(/dstone: DSOUNDSTART/, 120000, 'dsound start attempt ' + attempt);
    } catch (e) {
      console.log('RETRY dstone', attempt, 'stuck; killing pid=' + sp.pid);
      await killPid(sp.pid);
      await page.waitForTimeout(2000);
    }
  }
  if (dsDone) {
    await waitFor(/dstone: stopped/, 60000, 'dsound done');
    await page.waitForTimeout(1000);
  }
  const tap4 = await getTapData();
  let dsPeak = 0, dsZc = 0;
  if (tap4) {
    const d = Float32Array.from(tap4.samples);
    const d0 = Math.floor(sr * 0.5), d1 = Math.min(d0 + sr, d.length);
    for (let i = 0; i < d.length; i++) { const a = Math.abs(d[i]); if (a > dsPeak) dsPeak = a; }
    for (let i = d0 + 1; i < d1; i++) if ((d[i - 1] < 0) !== (d[i] < 0)) dsZc++;
  }
  const dsLogs = logs.filter(l => /dstone:/.test(l));
  summary.phases.dsound = { peak: dsPeak, freqHz: dsZc / 2, chunks: tap4?.times.length || 0, logs: dsLogs };
  console.log('PHASE4', JSON.stringify({ peak: dsPeak, freq: dsZc / 2, chunks: tap4?.times.length || 0 }));

  summary.errors = errors;
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await flush();
  console.log('SUMMARY', JSON.stringify(summary.phases, null, 1));
} catch (e) {
  summary.fatal = e.message;
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await flush();
  console.error('FATAL', e.message);
  process.exitCode = 1;
} finally {
  await browser.close();
  server.close();
}
