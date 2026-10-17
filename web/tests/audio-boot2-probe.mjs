// Audio verification: stage exe, then boot directly into audiotone.
// Click during Wine boot to unlock audio before the tone plays.
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';

const tag = process.env.TAG || `audio-boot2-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG bad');
const dir = `test-results/${tag}`;
await mkdir(dir, { recursive: true });
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [];
const logTimes = [];
page.on('console', m => { logs.push(`${m.type()}: ${m.text()}`); logTimes.push(Date.now()); });
page.on('pageerror', e => logs.push(`pageerror: ${e.message}`));
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const timeout = Number(process.env.TIMEOUT || 900000);
const baseUrl = `http://127.0.0.1:${server.address().port}/runtime/index.html`;

await page.addInitScript(() => {
  window.__bwAudio = { taps: [], ctx: null };
  const patch = (name) => {
    const RealAC = window[name];
    if (!RealAC) return;
    const wrapCtx = (ctx) => {
      window.__bwAudio.ctx = ctx;
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
        window.__bwAudio.taps.push({ tap, chunks, sampleRate: ctx.sampleRate, chunkLen: node.bufferSize });
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

const summary = { tag };
try {
  // Phase A: boot with dummy, stage the exe.
  await page.goto(`${baseUrl}?p=cmd%20%2Fc%20echo%20ready&session=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('READY-A');
  const bytes = await readFile('/tmp/audioprobe-tone.exe');
  const b64 = bytes.toString('base64');
  await page.evaluate(({ b64 }) => {
    const fs = window.bwRuntime.fs();
    fs.mkdirTree('/root/home/username');
    fs.writeFile('/root/home/username/audiotone.exe', Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
    const r = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/audiotone.exe']);
    if (r !== 1) throw new Error('register failed');
    // Also write to a path that survives navigation: localStorage can't hold binary.
    // Instead, stash base64 in sessionStorage for the next page load to re-stage.
    sessionStorage.setItem('bw_audio_exe', b64);
  }, { b64 });
  console.log('STAGED-A');

  // Phase B: navigate to audiotone boot. Re-stage from sessionStorage on load.
  await page.addInitScript(() => {
    window.addEventListener('DOMContentLoaded', () => {
      const b64 = sessionStorage.getItem('bw_audio_exe');
      if (b64 && window.bwRuntime) {
        const wait = setInterval(() => {
          if (window.bwRuntime?.ready?.()) {
            clearInterval(wait);
            const fs = window.bwRuntime.fs();
            try { fs.mkdirTree('/root/home/username'); } catch (e) {}
            fs.writeFile('/root/home/username/audiotone.exe', Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
            window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/audiotone.exe']);
          }
        }, 500);
      }
    });
  });
  logs.length = 0; logTimes.length = 0;
  const bootExe = encodeURIComponent('Z:\\home\\username\\audiotone.exe');
  await page.goto(`${baseUrl}?p=${bootExe}&session=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('READY-B');

  // Click during Wine boot (before audiotone starts).
  await page.waitForTimeout(20000);
  const canvasInfo = await page.evaluate(() => {
    const c = document.getElementById('canvas') || document.querySelector('canvas');
    if (!c) return null;
    const r = c.getBoundingClientRect();
    return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
  });
  if (canvasInfo) await page.mouse.click(canvasInfo.x, canvasInfo.y);
  await page.waitForTimeout(2000);
  const ctxState = await page.evaluate(() => window.__bwAudio?.ctx?.state || 'none');
  console.log('CLICK ctx=', ctxState);
  summary.clickCtx = ctxState;

  const waitFor = async (re, ms) => {
    const until = Date.now() + ms;
    while (Date.now() < until) {
      await flush();
      const idx = logs.findIndex(l => re.test(l));
      if (idx >= 0) return { t: logTimes[idx] };
      await page.waitForTimeout(1000);
    }
    throw new Error('timeout ' + re);
  };
  const ts = await waitFor(/audiotone: TONESTART/, 300000);
  console.log('TONESTART');
  await page.waitForTimeout(25000);

  const tap = await page.evaluate(() => {
    const t = (window.__bwAudio?.taps || [])[0];
    if (!t || !t.chunks.length) return null;
    const total = t.chunks.reduce((a, c) => a + c.data.length, 0);
    const out = new Float32Array(total);
    const times = [];
    let o = 0;
    for (const c of t.chunks) { out.set(c.data, o); o += c.data.length; times.push(c.t); }
    return { samples: Array.from(out), times, sr: t.sampleRate, chunkLen: t.chunkLen };
  });
  if (!tap) throw new Error('no tap data');

  const s = Float32Array.from(tap.samples);
  const sr = tap.sr;
  let onset = -1;
  for (let i = 0; i + 200 < s.length; i++) {
    let ok = true;
    for (let j = 0; j < 200; j += 10) if (Math.abs(s[i + j]) < 0.05) { ok = false; break; }
    if (ok) { onset = i; break; }
  }
  const w0 = onset >= 0 ? onset + Math.floor(sr * 0.7) : Math.floor(s.length / 2);
  const w1 = Math.min(w0 + sr, s.length);
  let zc = 0;
  for (let i = w0 + 1; i < w1; i++) if ((s[i - 1] < 0) !== (s[i] < 0)) zc++;
  let peak = 0, sum = 0;
  for (let i = w0; i < w1; i++) { const a = Math.abs(s[i]); if (a > peak) peak = a; sum += s[i] * s[i]; }
  const freq = zc / 2, rms = Math.sqrt(sum / Math.max(1, w1 - w0));
  let onsetT = null;
  if (onset >= 0 && tap.times.length) {
    const ci = Math.floor(onset / tap.chunkLen);
    onsetT = tap.times[Math.min(ci, tap.times.length - 1)] + (onset % tap.chunkLen) / sr * 1000;
  }
  summary.tone = { onset, freqHz: freq, peak: Number(peak.toFixed(4)), rms: Number(rms.toFixed(4)),
    latencyMs: onsetT !== null ? Math.round(onsetT - ts.t) : null, sr, totalSamples: s.length };
  console.log('TONE', JSON.stringify(summary.tone));

  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await flush();
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
