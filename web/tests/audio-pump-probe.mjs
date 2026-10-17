// Focused experiment: observe emscripten SDL audio pump mechanics.
// Q1: is an AudioContext created when the guest opens /dev/dsp (no click)?
// Q2: after a real click, does the queued audio drain / become audible?
// Q3: what creates ScriptProcessorNodes and when?
//
// Run: TAG=pump-probe-1 node web/tests/audio-pump-probe.mjs
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';

const tag = process.env.TAG || `audio-pump-probe-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('bad TAG');
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

const out = { tag, snapshots: [] };
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [];
  page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => logs.push('PAGEERROR: ' + e.message));
  const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
  const timeout = Number(process.env.TIMEOUT || 1800000);

  await page.addInitScript(() => {
    window.__bwWinId = Math.random().toString(36).slice(2);
    window.__bwInitCount = (window.__bwInitCount || 0) + 1;
    console.log('BW-AUDIO: init script ran #' + window.__bwInitCount + ', winId=' + window.__bwWinId);
    let _bwAudio = { taps: [], ctx: null, events: [] };
    Object.defineProperty(window, '__bwAudio', {
      get() { return _bwAudio; },
      set(v) {
        console.log('BW-AUDIO: __bwAudio REPLACED! stack=' + new Error().stack.split('\n').slice(1, 4).join(' <- '));
        _bwAudio = v;
      },
      configurable: true,
    });
    const patch = (name) => {
      const RealAC = window[name];
      if (!RealAC) return;
      const wrapCtx = (ctx) => {
        const before = window.__bwAudio;
        console.log('BW-AUDIO: constructed state=' + ctx.state + ' __bwAudio=' + (typeof before) + ' winId=' + (window.__bwWinId || 'none'));
        if (!before) window.__bwAudio = { taps: [], ctx: null, events: [] };
        window.__bwAudio.ctx = ctx;
        console.log('BW-AUDIO: after-set ctxSet=' + (window.__bwAudio.ctx === ctx) + ' sameObj=' + (window.__bwAudio === before));
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

  const bytes = await readFile('/tmp/audioprobe/audiotone.exe');
  await page.evaluate(({ b64 }) => {
    const fs = window.bwRuntime.fs();
    fs.mkdirTree('/root/home/username');
    fs.writeFile('/root/home/username/audiotone.exe', Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
    if (window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/audiotone.exe']) !== 1)
      throw new Error('register failed');
  }, { b64: bytes.toString('base64') });
  console.log('STAGED');

  const bootstrapUntil = Date.now() + 120000;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) break;
    await flush();
    await page.waitForTimeout(500);
  }

  const snap = async (label) => {
    const s = await page.evaluate(() => ({
      winId: window.__bwWinId || 'none',
      bwAudioType: typeof window.__bwAudio,
      ctxIsNull: window.__bwAudio ? window.__bwAudio.ctx === null : 'n/a',
      nEvents: window.__bwAudio ? window.__bwAudio.events.length : -1,
      ctxState: window.__bwAudio?.ctx?.state || 'none',
      nTaps: (window.__bwAudio?.taps || []).length,
      tapChunks: (window.__bwAudio?.taps || []).map(t => t.chunks.length),
      tapSr: (window.__bwAudio?.taps || []).map(t => t.sampleRate),
      events: window.__bwAudio?.events || [],
    }));
    s.label = label;
    s.openAudioLines = logs.filter(l => /openAudio:|Failed to open audio/.test(l)).length;
    out.snapshots.push(s);
    console.log('SNAP', label, JSON.stringify({ win: s.winId, bwT: s.bwAudioType, ctxNull: s.ctxIsNull, nEv: s.nEvents, ctx: s.ctxState, taps: s.nTaps, chunks: s.tapChunks, events: s.events.length, audioLines: s.openAudioLines }));
  };

  const waitForLog = async (re, ms) => {
    const until = Date.now() + ms;
    while (Date.now() < until) {
      await flush();
      if (logs.some(l => re.test(l))) return true;
      await page.waitForTimeout(500);
    }
    return false;
  };

  await snap('page-loaded');
  // wineserver pin
  await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\windows\\\\system32\\\\ping.exe\n127.0.0.1\n-n\n100', 'LD_LIBRARY_PATH=/home/username']));
  await page.waitForTimeout(3000);

  // spawn tone WITHOUT click
  await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\audiotone.exe', 'LD_LIBRARY_PATH=/home/username']));
  const gotTone = await waitForLog(/audiotone: TONESTART/, 300000);
  console.log('TONESTART seen:', gotTone);
  await page.waitForTimeout(5000);
  await flush();
  await snap('after-tonestart-no-click');

  // real click
  const ci = await page.evaluate(() => {
    const c = document.getElementById('canvas') || document.querySelector('canvas');
    const r = c.getBoundingClientRect();
    return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
  });
  await page.mouse.click(ci.x, ci.y);
  await page.waitForTimeout(5000);
  await flush();
  await snap('after-click');

  // capture tap audio, look for 440Hz (loop peak: spread blows the stack)
  const tap = await page.evaluate(() => {
    const t = (window.__bwAudio?.taps || [])[0];
    if (!t) return null;
    const total = t.chunks.reduce((a, c) => a + c.data.length, 0);
    const o = new Float32Array(total);
    let p = 0;
    for (const c of t.chunks) { o.set(c.data, p); p += c.data.length; }
    let peak = 0;
    for (let i = 0; i < o.length; i++) { const a = Math.abs(o[i]); if (a > peak) peak = a; }
    const zcFreq = (a, b) => {
      let z = 0;
      for (let i = a + 1; i < b; i++) if ((o[i - 1] < 0) !== (o[i] < 0)) z++;
      return z / 2;
    };
    // first 1s vs last 1s of capture: did the queued tone drain right after click?
    const sr = t.sampleRate;
    return {
      n: total, sr, peak, chunks: t.chunks.length,
      freqFirst1s: zcFreq(0, Math.min(sr, o.length)),
      freqLast1s: zcFreq(Math.max(0, o.length - sr), o.length),
    };
  });
  console.log('TAP', JSON.stringify(tap));
  out.tap = tap;

  await writeFile(`${dir}/report.json`, JSON.stringify(out, null, 2));
  await flush();
  console.log('DONE');
} catch (e) {
  out.fatal = e.message;
  await writeFile(`${dir}/report.json`, JSON.stringify(out, null, 2));
  console.error('FATAL', e.message);
  process.exitCode = 1;
} finally {
  await browser.close();
  server.close();
  await slot.release();
}
