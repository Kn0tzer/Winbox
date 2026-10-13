// Direct /dev/dsp audio verification probe.
//
// Stages dsptone.exe: a Windows PE that opens Z:\dev\dsp (boxedwine DevDsp
// virtual device) and WriteFiles a 440Hz U8 sine at the device defaults
// (11025Hz mono), bypassing Wine's waveOut/DirectSound entirely.
//
// Chain under test:
//   dsptone.exe -> CreateFile(Z:\dev\dsp) -> DevDsp::writeNative ->
//   KDspAudioSdl::writeAudio -> F32 ring -> ScriptProcessorNode -> tap
//
// Success = non-zero captured tone with ~440Hz spectrum. This proves the
// Boxedwine audio backend transports real data end-to-end and isolates the
// remaining audio bug in Wine's driver layer.
//
// Run from repo root:
//   TAG=audio-dsptone-1 node web/tests/audio-dsptone.mjs
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';

const tag = process.env.TAG || `audio-dsptone-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/${tag}`;
await mkdir(dir, { recursive: true });

await waitForBuildIdle();
const slot = await acquireProbeSlot();

const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
  headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
    '--enable-features=Vulkan'],
});

const summary = { tag, phases: {}, pass: {} };
const logs = [], errors = [];
const logTimes = [];
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  page.on('console', m => { const t = Date.now(); const txt = `${m.type()}: ${m.text()}`; logs.push(txt); logTimes.push({ t, txt }); });
  page.on('pageerror', e => errors.push(e.message));
  const timeout = Number(process.env.TIMEOUT || 1800000);

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
            chunks.push({ t: performance.now(),
              data: new Float32Array(e.inputBuffer.getChannelData(0)),
              out: new Float32Array(e.outputBuffer.getChannelData(0)) });
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

  const exeName = process.env.DSP_EXE || 'dsptone.exe';
  const exeTag = exeName.replace(/\.exe$/, '');
  const startMarker = new RegExp(exeTag + ': WRITESTART');
  const doneMarker = new RegExp(exeTag + ': done');
  const bytes = await readFile('/tmp/audioprobe/' + exeName);
  const b64 = bytes.toString('base64');
  await page.evaluate(({ b64, exeName }) => {
    const fs = window.bwRuntime.fs();
    fs.mkdirTree('/root/home/username');
    fs.writeFile('/root/home/username/' + exeName, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
    const registered = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + exeName]);
    if (registered !== 1) throw new Error('Guest VFS registration failed for ' + exeName);
  }, { b64, exeName });
  console.log('STAGED ' + exeName);

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
  const getTapData = () => page.evaluate(() => {
    const taps = window.__bwAudio?.taps || [];
    // The bridge node generates audio (0-input); SDL's node has input.
    // Capture both streams per tap and let the analysis pick the one with signal.
    return taps.map((t, ti) => {
      const pack = (key) => {
        const total = t.chunks.reduce((a, c) => a + c[key].length, 0);
        const out = new Float32Array(total);
        const times = new Float64Array(t.chunks.length);
        let o = 0;
        t.chunks.forEach((c, i) => { out.set(c[key], o); o += c[key].length; times[i] = c.t; });
        let peak = 0;
        for (let i = 0; i < out.length; i += 7) { const a = Math.abs(out[i]); if (a > peak) peak = a; }
        return { samples: Array.from(out), times: Array.from(times), peak };
      };
      return { ti, sr: t.sampleRate, chunkLen: t.chunks[0]?.data.length || 0, in: pack('data'), out: pack('out') };
    });
  });
  const clearTap = () => page.evaluate(() => { for (const t of (window.__bwAudio?.taps || [])) t.chunks.length = 0; });

  console.log('PHASE 1: click to unlock audio');
  await clearTap();
  const canvasInfo = await page.evaluate(() => {
    const c = document.getElementById('canvas') || document.querySelector('canvas');
    if (!c) return { found: false };
    const r = c.getBoundingClientRect();
    c.addEventListener('mousedown', () => { window.__bwClickHit = true; }, { once: true });
    return { found: true, x: r.x + r.width / 2, y: r.y + r.height / 2 };
  });
  if (canvasInfo.found) await page.mouse.click(canvasInfo.x, canvasInfo.y);
  else await page.mouse.click(640, 450);
  await page.waitForTimeout(2000);
  const clickHit = await page.evaluate(() => !!window.__bwClickHit);
  const ctxState = await page.evaluate(() => window.__bwAudio?.ctx?.state || 'none');
  summary.phases.click = { clickHit, ctxState };
  summary.pass.phase1_unlock = clickHit && ctxState === 'running';
  console.log('PHASE1', JSON.stringify(summary.phases.click));

  console.log('PHASE 2: spawn ' + exeName);
  await clearTap();
  const sp = await spawnExe(exeName);
  let ws = null;
  try {
    ws = await waitFor(startMarker, 120000, 'writestart', sp.startIdx);
  } catch (e) {
    console.log('DSP open/write failed; dumping log lines:');
    logs.slice(sp.startIdx).filter(l => new RegExp(exeTag + ':').test(l)).forEach(l => console.log('  ' + l));
    throw e;
  }
  const pumpArmed = logs.slice(sp.startIdx).some(l => /bwPump: bridge armed/.test(l));
  const firstPush = logs.slice(sp.startIdx).find(l => /bwPump: first push/.test(l)) || null;
  // The guest may stall mid-write (under diagnosis); the tap captures whatever
  // reached the ring regardless. Give the writes 45s to flow, then analyze the
  // tap unconditionally; afterwards, wait briefly for clean guest completion.
  await page.waitForTimeout(45000);
  let doneSeen = false;
  try {
    await waitFor(doneMarker, 60000, 'done', sp.startIdx);
    doneSeen = true;
  } catch (e) {
    console.log('NOTE: guest did not finish within window; analyzing tap anyway');
  }
  await page.waitForTimeout(5000);
  const taps = await getTapData();
  await writeFile(`${dir}/tap.json`, JSON.stringify(
    taps.map(t => ({ ti: t.ti, sr: t.sr, nIn: t.in.samples.length, nOut: t.out.samples.length, peakIn: t.in.peak, peakOut: t.out.peak }))));

  // Pick the (tap, stream) with the strongest signal.
  let best = null, bestPeak = 0;
  for (const t of taps || []) {
    for (const key of ['in', 'out']) {
      if (t[key].peak > bestPeak) { bestPeak = t[key].peak; best = { t, key }; }
    }
  }
  let onset = -1, freq = 0, peak = 0, rms = 0, latencyMs = null, sr = 0;
  let totalSamples = 0, bestDesc = null;
  if (best) {
    const tap = best.t, stream = tap[best.key];
    bestDesc = `tap${tap.ti}.${best.key}`;
    const s = Float32Array.from(stream.samples);
    sr = tap.sr; totalSamples = s.length;
    for (let i = 0; i + 200 < s.length; i++) {
      let ok = true;
      for (let j = 0; j < 200; j += 10) if (Math.abs(s[i + j]) < 0.05) { ok = false; break; }
      if (ok) { onset = i; break; }
    }
    const w0 = onset >= 0 ? onset : 0, w1 = Math.min(w0 + sr, s.length);
    let zc = 0;
    for (let i = w0 + 1; i < w1; i++) if ((s[i - 1] < 0) !== (s[i] < 0)) zc++;
    freq = zc / 2;
    let sum = 0, n2 = 0;
    for (let i = w0; i < w1; i++) { const a = Math.abs(s[i]); if (a > peak) peak = a; sum += s[i] * s[i]; n2++; }
    rms = n2 ? Math.sqrt(sum / n2) : 0;
    if (onset >= 0 && tap.chunkLen > 0 && stream.times.length > 0) {
      const ci = Math.floor(onset / tap.chunkLen);
      const onsetT = stream.times[Math.min(ci, stream.times.length - 1)] + (onset % tap.chunkLen) / sr * 1000;
      latencyMs = onsetT - ws.t;
    }
  }
  const dspLogLines = logs.slice(sp.startIdx).filter(l => new RegExp(exeTag + ':').test(l)).slice(0, 12);
  const ctxState2 = await page.evaluate(() => window.__bwAudio?.ctx?.state || 'none');
  if (best) {
    const { writeFile: wf } = await import('node:fs/promises');
    const raw = Buffer.from(Float32Array.from(best.t[best.key].samples).buffer);
    await wf(`${dir}/best.raw`, raw);
    console.log('SAVED best.raw', bestDesc, raw.length, 'bytes');
  }
  summary.phases.dsp = { onset, freqHz: freq, peak, rms, latencyMs, sr, totalSamples, bestDesc, pumpArmed, firstPush, dspLogLines, doneSeen, ctxState2 };
  summary.pass.phase2_freq = freq >= 430 && freq <= 450;
  summary.pass.phase2_audible = peak > 0.05 && rms > 0.01;
  summary.pass.phase2_pump = pumpArmed;
  summary.pass.phase1_unlock = clickHit && totalSamples > 40000;
  summary.errors = errors;
  summary.overallPass = summary.pass.phase1_unlock && summary.pass.phase2_freq && summary.pass.phase2_audible;
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await flush();
  console.log('SUMMARY', JSON.stringify(summary, null, 1));
} catch (e) {
  summary.fatal = e.message;
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await flush();
  console.error('FATAL', e.message);
  process.exitCode = 1;
} finally {
  await browser.close();
  server.close();
  await slot.release();
}
