// Streamlined audio verification: boot -> click -> spawn tone -> capture.
// Skips the pre-click spawn (gate already verified) to minimize Wine spawns.
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';

const tag = process.env.TAG || `audio-tone-${Date.now()}`;
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
page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
page.on('pageerror', e => logs.push(`pageerror: ${e.message}`));
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const timeout = Number(process.env.TIMEOUT || 900000);

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
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  const bytes = await readFile('/tmp/audioprobe-tone.exe');
  const b64 = bytes.toString('base64');
  await page.evaluate(({ b64 }) => {
    const fs = window.bwRuntime.fs();
    fs.mkdirTree('/root/home/username');
    fs.writeFile('/root/home/username/audiotone.exe', Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
    const r = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/audiotone.exe']);
    if (r !== 1) throw new Error('register failed');
  }, { b64 });
  console.log('STAGED');

  const bootUntil = Date.now() + 300000;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootUntil) break;
    await flush();
    await page.waitForTimeout(500);
  }
  const bootOk = logs.some(l => /^log: vkboot\s*$/.test(l));
  console.log('BOOT', bootOk ? 'OK' : 'TIMEOUT');
  if (!bootOk) throw new Error('bootstrap failed: vkboot never appeared');
  // Let the system settle after the heavy bootstrap before spawning.
  console.log('SETTLING...');
  await page.waitForTimeout(30000);

  // Click FIRST (before any spawn) to unlock audio.
  const canvasInfo = await page.evaluate(() => {
    const c = document.getElementById('canvas') || document.querySelector('canvas');
    if (!c) return null;
    const r = c.getBoundingClientRect();
    return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
  });
  await page.mouse.click(canvasInfo.x, canvasInfo.y);
  await page.waitForTimeout(2000);
  const ctxState = await page.evaluate(() => window.__bwAudio?.ctx?.state || 'none');
  console.log('CLICK ctx=', ctxState);
  summary.clickCtx = ctxState;

  // Spawn with retries.
  const waitFor = async (re, ms) => {
    const until = Date.now() + ms;
    while (Date.now() < until) {
      await flush();
      const hit = logs.find(l => re.test(l));
      if (hit) {
        const idx = logs.indexOf(hit);
        return { hit, t: Date.now() };
      }
      await page.waitForTimeout(500);
    }
    throw new Error('timeout ' + re);
  };
  let ts = null, pid = 0;
  for (let a = 1; a <= 4 && !ts; a++) {
    const startLen = logs.length;
    await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
      ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\audiotone.exe', 'LD_LIBRARY_PATH=/home/username']));
    const m = logs.slice(startLen).find(l => /spawned pid=(\d+)/.test(l));
    // poll for pid
    const pu = Date.now() + 15000;
    while (Date.now() < pu && !pid) {
      const mm = logs.slice(startLen).find(l => /spawned pid=(\d+)/.test(l));
      if (mm) pid = Number(mm.match(/spawned pid=(\d+)/)[1]);
      else await page.waitForTimeout(500);
    }
    console.log('SPAWN attempt', a, 'pid=', pid);
    try {
      ts = await waitFor(/audiotone: TONESTART/, 120000);
      console.log('TONESTART on attempt', a);
    } catch (e) {
      console.log('attempt', a, 'stuck; killing');
      if (pid) await page.evaluate((p) => { try { window.bwRuntime.call('bw64_kill', ['number'], [p]); } catch (e) {} }, pid);
      pid = 0;
      await page.waitForTimeout(3000);
    }
  }
  if (!ts) throw new Error('spawn failed after 4 attempts');
  summary.spawnPid = pid;

  // Wait for tone to play, then capture tap.
  await page.waitForTimeout(25000);
  const tap = await page.evaluate(() => {
    const t = (window.__bwAudio?.taps || [])[0];
    if (!t) return null;
    const total = t.chunks.reduce((a, c) => a + c.data.length, 0);
    const out = new Float32Array(total);
    const times = [];
    let o = 0;
    for (const c of t.chunks) { out.set(c.data, o); o += c.data.length; times.push(c.t); }
    return { samples: Array.from(out), times, sr: t.sampleRate, chunkLen: t.chunks[0]?.data.length || 0 };
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
  const freq = zc / 2, rms = Math.sqrt(sum / (w1 - w0));
  let onsetT = null;
  if (onset >= 0) {
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
