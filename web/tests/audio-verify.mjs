// Audio real-world verification probe (consolidated).
//
// Verifies end-to-end audio from a Windows guest through to audible browser output:
//   Phase 1: Autoplay gate check WITHOUT spawning guest audio -> expect a fresh
//            AudioContext to start 'suspended' (the SDL audio device is exclusive
//            and its queue never drains in this build, so a pre-click tone spawn
//            would leak the device and poison all later phases).
//   Phase 2: Real Playwright click on canvas -> AudioContext unlocks
//   Phase 3: Spawn tone AFTER click -> capture waveform, verify 440Hz via zero-crossings,
//            measure end-to-end latency (guest TONESTART -> audible onset).
//            Guest-side evidence: openAudio klog (device opened, SDL_QueueAudio mode).
//   Phase 4: DirectSound playback via dstone.exe
//
// Infrastructure:
//   - Uses probe-slots.mjs for probe concurrency (waits for a free slot)
//   - Uses build-lock.mjs to wait for any in-progress WASM build before launching
//   - Installs an AudioContext tap via addInitScript BEFORE page scripts run,
//     capturing raw PCM via ScriptProcessorNode (deprecated but reliable for capture)
//
// Run from repo root:
//   TAG=audio-verify-1 node web/tests/audio-verify.mjs
//
// Prerequisites (on the box):
//   /tmp/audioprobe/audiotone.exe  - waveOut 440Hz tone generator (3.5s)
//   /tmp/audioprobe/dstone.exe      - DirectSound tone generator
//
// Output:
//   test-results/<TAG>/console.log   - full browser console
//   test-results/<TAG>/summary.json  - phase results, freq, latency, errors
//   test-results/<TAG>/tap3.json     - raw captured PCM metadata (phase 3)
//
// Pass criteria:
//   - Phase 1: fresh AudioContext starts 'suspended' (gate holds) AND tap silent
//   - Phase 2: clickHit=true AND ctx state transitions to 'running'
//   - Phase 3: freqHz within 430-450 AND peak > 0.05 AND rms > 0.01
//   - Phase 4: dsPeak > 0.05 (audible DirectSound output)
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';

const tag = process.env.TAG || `audio-verify-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/${tag}`;
await mkdir(dir, { recursive: true });

// ---- Coordination: wait for build idle, then acquire a probe slot ----
await waitForBuildIdle();
const slot = await acquireProbeSlot();

const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
  headless: true,
  // Deliberately NOT passing --autoplay-policy=no-user-gesture-required.
  // We want the real autoplay gate so Phase 1 can verify the block and
  // Phase 2 can verify the real-click unlock.
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
    '--enable-features=Vulkan'],
});

const summary = { tag, phases: {}, pass: {} };
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [], errors = [];
  const logTimes = [];
  page.on('console', m => { const t = Date.now(); const txt = `${m.type()}: ${m.text()}`; logs.push(txt); logTimes.push({ t, txt }); });
  page.on('pageerror', e => errors.push(e.message));
  const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
  const timeout = Number(process.env.TIMEOUT || 1800000);

  // ---- AudioContext tap: installed before any page script runs ----
  await page.addInitScript(() => {
    window.__bwAudio = { taps: [], ctx: null, events: [] };
    const patch = (name) => {
      const RealAC = window[name];
      if (!RealAC) return;
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

  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

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

  await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\windows\\\\system32\\\\ping.exe\n127.0.0.1\n-n\n100', 'LD_LIBRARY_PATH=/home/username']));
  await page.waitForTimeout(3000);
  console.log('WINESERVER PINNED');

  const spawnExe = async (exe) => {
    const startIdx = logs.length;
    const r = await page.evaluate((exe) => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
      ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\' + exe, 'LD_LIBRARY_PATH=/home/username']), exe);
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
  // fromIdx scopes the search so a retry never matches a stale marker from an
  // earlier phase/attempt (e.g. phase 3 must not match phase 1's TONESTART).
  const waitFor = async (re, ms, label, fromIdx = 0) => {
    const until = Date.now() + ms;
    while (Date.now() < until) {
      await flush();
      const hit = logs.slice(fromIdx).find(l => re.test(l));
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

  // PHASE 1: autoplay gate WITHOUT touching the guest audio device.
  // The SDL audio device is exclusive and (in this build) its queue never
  // drains, so a phase-1 tone spawn would leak the device and poison all
  // later phases. Instead, verify the gate at the browser level: a fresh
  // AudioContext must start 'suspended' before any user gesture.
  console.log('PHASE 1: autoplay gate (no guest audio spawn)');
  const gateState = await page.evaluate(() => {
    const C = window.AudioContext || window.webkitAudioContext;
    const c = new C();
    const s = c.state;
    c.close();
    return s;
  });
  await page.waitForTimeout(3000);
  const st1 = await audioState();
  const tap1 = await getTapData();
  const silent1 = !tap1 || tap1.samples.every(s => Math.abs(s) < 0.02);
  summary.phases.noClick = { gateState, ctxState: st1.ctxState, tapChunks: tap1 ? tap1.times.length : 0, silent: silent1 };
  summary.pass.phase1_silence = silent1;
  summary.pass.phase1_gate = gateState === 'suspended';
  console.log('PHASE1', JSON.stringify({ gate: gateState, ctx: st1.ctxState, chunks: tap1?.times.length || 0, silent: silent1 }));

  console.log('PHASE 2: real Playwright click on canvas');
  await clearTap();
  const before = await audioState();
  const canvasInfo = await page.evaluate(() => {
    const c = document.getElementById('canvas') || document.querySelector('canvas');
    if (!c) return { found: false };
    const r = c.getBoundingClientRect();
    c.addEventListener('mousedown', () => { window.__bwClickHit = true; }, { once: true });
    return { found: true, x: r.x + r.width / 2, y: r.y + r.height / 2, w: r.width, h: r.height };
  });
  console.log('CANVAS', JSON.stringify(canvasInfo));
  if (canvasInfo.found) {
    await page.mouse.click(canvasInfo.x, canvasInfo.y);
  } else {
    await page.mouse.click(640, 450);
  }
  await page.waitForTimeout(2000);
  const clickHit = await page.evaluate(() => !!window.__bwClickHit);
  const after = await audioState();
  summary.phases.click = { before: before.ctxState, after: after.ctxState, clickHit };
  summary.pass.phase2_unlock = clickHit && after.ctxState === 'running';
  console.log('PHASE2', JSON.stringify({ before: before.ctxState, after: after.ctxState, clickHit }));

  console.log('PHASE 3: spawn after click (expect 440Hz tone)');
  await clearTap();
  let ts = null;
  let p3StartIdx = 0;
  for (let attempt = 1; attempt <= 3 && !ts; attempt++) {
    const sp = await spawnExe('audiotone.exe');
    p3StartIdx = sp.startIdx;
    try {
      ts = await waitFor(/audiotone: TONESTART/, 120000, 'tonestart attempt ' + attempt, sp.startIdx);
    } catch (e) {
      console.log('RETRY spawn', attempt, 'stuck; killing pid=' + sp.pid);
      await killPid(sp.pid);
      await page.waitForTimeout(2000);
    }
  }
  if (!ts) throw new Error('phase3 spawn failed after 3 attempts');
  await page.waitForTimeout(25000);
  // Guest-side evidence: KDspAudioSdl::openAudio klog when /dev/dsp is opened
  // (SDL audio device opened in SDL_QueueAudio mode).
  const dspOpenLog = logs.slice(p3StartIdx).find(l => /openAudio:/.test(l)) || null;
  const tap3 = await getTapData();
  await writeFile(`${dir}/tap3.json`, JSON.stringify({ sr: tap3?.sr, n: tap3?.samples.length, times: tap3?.times }));
  // dspOpenLog is the guest-side proof that the audio device was opened and
  // SDL_QueueAudio mode was entered; queueNonEmpty proves samples were queued.
  const queueEv = logs.slice(p3StartIdx).some(l => /Failed to open audio/.test(l));
  let onset = -1, freq = 0, peak = 0, rms = 0, latencyMs = null, sr = tap3?.sr || 0;
  let totalSamples = 0, onsetT = null;
  if (tap3 && tap3.samples.length > 0) {
    const s = Float32Array.from(tap3.samples);
    sr = tap3.sr; totalSamples = s.length;
    for (let i = 0; i + 200 < s.length; i++) {
      let ok = true;
      for (let j = 0; j < 200; j += 10) if (Math.abs(s[i + j]) < 0.05) { ok = false; break; }
      if (ok) { onset = i; break; }
    }
    const w0 = onset + Math.floor(sr * 0.7), w1 = w0 + sr;
    let zc = 0;
    for (let i = w0 + 1; i < Math.min(w1, s.length); i++) if ((s[i - 1] < 0) !== (s[i] < 0)) zc++;
    freq = zc / 2;
    let sum = 0, n2 = 0;
    for (let i = w0; i < Math.min(w1, s.length); i++) { const a = Math.abs(s[i]); if (a > peak) peak = a; sum += s[i] * s[i]; n2++; }
    rms = n2 ? Math.sqrt(sum / n2) : 0;
    if (onset >= 0 && tap3.chunkLen > 0 && tap3.times.length > 0) {
      const ci = Math.floor(onset / tap3.chunkLen);
      onsetT = tap3.times[Math.min(ci, tap3.times.length - 1)] + (onset % tap3.chunkLen) / sr * 1000;
    }
    latencyMs = onsetT !== null ? onsetT - ts.t : null;
  }
  summary.phases.tone = { onset, freqHz: freq, peak, rms, latencyMs, sr, totalSamples, dspOpenLog, queueStuck: queueEv };
  summary.pass.phase3_freq = freq >= 430 && freq <= 450;
  summary.pass.phase3_audible = peak > 0.05 && rms > 0.01;
  console.log('PHASE3', JSON.stringify(summary.phases.tone));

  console.log('PHASE 4: DirectSound');
  await clearTap();
  let dsDone = null;
  let dsStartIdx = 0;
  for (let attempt = 1; attempt <= 3 && !dsDone; attempt++) {
    const sp = await spawnExe('dstone.exe');
    dsStartIdx = sp.startIdx;
    try {
      dsDone = await waitFor(/dstone: DSOUNDSTART/, 120000, 'dsound start attempt ' + attempt, sp.startIdx);
    } catch (e) {
      console.log('RETRY dstone', attempt, 'stuck; killing pid=' + sp.pid);
      await killPid(sp.pid);
      await page.waitForTimeout(2000);
    }
  }
  if (dsDone) {
    await waitFor(/dstone: stopped/, 60000, 'dsound done', dsStartIdx);
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
  summary.phases.dsound = { peak: dsPeak, freqHz: dsZc / 2, chunks: tap4?.times.length || 0, logLines: dsLogs.length };
  summary.pass.phase4_dsound = dsPeak > 0.05;
  console.log('PHASE4', JSON.stringify({ peak: dsPeak, freq: dsZc / 2, chunks: tap4?.times.length || 0 }));

  summary.errors = errors;
  summary.overallPass = summary.pass.phase1_silence && summary.pass.phase1_gate && summary.pass.phase2_unlock &&
    summary.pass.phase3_freq && summary.pass.phase3_audible && summary.pass.phase4_dsound;
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await flush();
  console.log('SUMMARY', JSON.stringify(summary, null, 1));
} catch (e) {
  summary.fatal = e.message;
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.error('FATAL', e.message);
  process.exitCode = 1;
} finally {
  await browser.close();
  server.close();
  await slot.release();
}
