// audio-pump-diag.mjs — runtime diagnosis of the SDL queue -> Web Audio drain.
// Instruments the DRIVER's ScriptProcessorNode onaudioprocess (via accessor
// interception on the created node) to answer decisively:
//   - does the driver's onaudioprocess fire at all? (pump wiring)
//   - if it fires, does the C side write non-zero samples into outputBuffer?
//     (HandleAudioProcess feed vs silence gate / NaN poisoning)
//   - are there dynCall/EM_ASM exceptions in the audio path?
//   - was the silenceTimer path taken? (setInterval wrapper)
// Run from repo root: TAG=audio-pump-diag-1 node web/tests/audio-pump-diag.mjs
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';

const tag = process.env.TAG || `audio-pump-diag-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/${tag}`;
await mkdir(dir, { recursive: true });

await waitForBuildIdle();
const slot = await acquireProbeSlot();

const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
  headless: true,
  // Real autoplay policy (no override): we want the genuine gate behavior.
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
    '--enable-features=Vulkan'],
});

const summary = { tag };
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [], errors = [];
  page.on('console', m => { logs.push(`${m.type()}: ${m.text()}`); });
  page.on('pageerror', e => errors.push(e.message));
  const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
  const timeout = Number(process.env.TIMEOUT || 1800000);

  await page.addInitScript(() => {
    window.__bwAudio = { taps: [], nodeRecs: [], ctx: null, events: [], setIntervals: [] };
    // Detect SDL's silenceTimer path (setInterval ~=92.9ms at open time).
    const origSI = window.setInterval.bind(window);
    window.setInterval = function (fn, ms, ...rest) {
      const id = origSI(fn, ms, ...rest);
      try {
        window.__bwAudio.setIntervals.push({ id, ms, t: Math.round(performance.now()), fn: String(fn).slice(0, 160) });
      } catch (e) {}
      return id;
    };
    const origCI = window.clearInterval.bind(window);
    window.clearInterval = function (id) {
      try { window.__bwAudio.setIntervals.push({ cleared: id, t: Math.round(performance.now()) }); } catch (e) {}
      return origCI(id);
    };
    const patch = (name) => {
      const RealAC = window[name];
      if (!RealAC) return;
      const wrapCtx = (ctx) => {
        window.__bwAudio.ctx = ctx;
        window.__bwAudio.events.push({ t: Math.round(performance.now()), ev: 'ctx-created', state: ctx.state });
        ctx.addEventListener('statechange', () => {
          window.__bwAudio.events.push({ t: Math.round(performance.now()), ev: 'statechange', state: ctx.state });
        });
        const orig = ctx.createScriptProcessor.bind(ctx);
        ctx.createScriptProcessor = function (bufferSize, inCh, outCh) {
          const node = orig(bufferSize, inCh, outCh);
          // Intercept onaudioprocess assignment: the SDL driver assigns its
          // handler AFTER createScriptProcessor returns.
          const rec = { fires: 0, peak: 0, nan: false, errors: [], bufSize: node.bufferSize, inCh, outCh, assigned: false };
          window.__bwAudio.nodeRecs.push(rec);
          // Capture the IDL setter from the prototype chain BEFORE shadowing:
          // assigning node.onaudioprocess must still reach the internal slot,
          // otherwise the audio thread never invokes the handler (and we would
          // falsely conclude the pump never fires).
          let idlDesc = null;
          for (let o = node; o && !idlDesc; o = Object.getPrototypeOf(o)) {
            const d = Object.getOwnPropertyDescriptor(o, 'onaudioprocess');
            if (d && (d.set || d.value !== undefined)) idlDesc = d;
          }
          let handler = null;
          Object.defineProperty(node, 'onaudioprocess', {
            configurable: true, enumerable: true,
            get() { return handler; },
            set(fn) {
              rec.assigned = !!fn;
              handler = fn ? function (e) {
                rec.fires++;
                try { fn.call(this, e); }
                catch (err) { rec.errors.push(String((err && err.stack) || err).slice(0, 400)); throw err; }
                try {
                  const ch = e.outputBuffer.getChannelData(0);
                  for (let i = 0; i < ch.length; i += 4) {
                    const v = ch[i];
                    if (Number.isNaN(v)) rec.nan = true;
                    else { const a = Math.abs(v); if (a > rec.peak) rec.peak = a; }
                  }
                } catch (err) {}
              } : null;
              // Reach through to the real IDL setter so the internal slot is set.
              try {
                if (idlDesc && idlDesc.set) idlDesc.set.call(node, handler);
                else if (idlDesc) node.__proto__ && (Object.getPrototypeOf(node).onaudioprocess = handler);
              } catch (err) { rec.errors.push('idlset: ' + String(err).slice(0, 200)); }
            }
          });
          // tap (same design as audio-verify.mjs)
          const tap = orig(bufferSize || 4096, 1, 1);
          const zg = ctx.createGain(); zg.gain.value = 0;
          const chunks = [];
          tap.onaudioprocess = (e) => {
            chunks.push({ t: Math.round(performance.now()), data: new Float32Array(e.inputBuffer.getChannelData(0)) });
          };
          node.connect(tap);
          tap.connect(zg); zg.connect(ctx.destination);
          window.__bwAudio.taps.push({ node, tap, chunks, sampleRate: ctx.sampleRate, nodeBuf: node.bufferSize });
          window.__bwAudio.events.push({ t: Math.round(performance.now()), ev: 'scriptprocessor-created', buf: node.bufferSize, sr: ctx.sampleRate, inCh, outCh });
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

  {
    const bytes = await readFile('/tmp/audioprobe/audiotone.exe');
    const b64 = bytes.toString('base64');
    await page.evaluate(({ b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/audiotone.exe', Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const registered = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/audiotone.exe']);
      if (registered !== 1) throw new Error('Guest VFS registration failed');
    }, { b64 });
  }
  console.log('STAGED');

  const bootstrapUntil = Date.now() + 180000;
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

  const canvasInfo = await page.evaluate(() => {
    const c = document.getElementById('canvas') || document.querySelector('canvas');
    if (!c) return { found: false };
    const r = c.getBoundingClientRect();
    return { found: true, x: r.x + r.width / 2, y: r.y + r.height / 2 };
  });
  if (canvasInfo.found) await page.mouse.click(canvasInfo.x, canvasInfo.y);
  else await page.mouse.click(640, 450);
  await page.waitForTimeout(2000);
  console.log('CTX AFTER CLICK:', await page.evaluate(() => window.__bwAudio?.ctx?.state || 'none'));

  const startIdx = logs.length;
  await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\audiotone.exe', 'LD_LIBRARY_PATH=/home/username']));
  let ts = null;
  {
    const until = Date.now() + 120000;
    while (Date.now() < until && !ts) {
      await flush();
      const hit = logs.slice(startIdx).find(l => /audiotone: TONESTART/.test(l));
      if (hit) ts = hit;
      else await page.waitForTimeout(500);
    }
  }
  if (!ts) throw new Error('no TONESTART');
  console.log('TONESTART seen');
  await page.waitForTimeout(18000);
  await flush();

  // Second spawn: if the pump drained the queue, the drain timer will have
  // closed the device and this open succeeds. If the queue never drains,
  // this open fails with "Audio device already open".
  console.log('SECOND SPAWN (drain check)');
  const startIdx2 = logs.length;
  await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\audiotone.exe', 'LD_LIBRARY_PATH=/home/username']));
  let ts2 = null;
  {
    const until = Date.now() + 60000;
    while (Date.now() < until && !ts2) {
      await flush();
      const hit = logs.slice(startIdx2).find(l => /audiotone: TONESTART/.test(l));
      if (hit) ts2 = hit;
      else await page.waitForTimeout(500);
    }
  }
  // Bridge-state inspection: did bwAudioEnsureBridge run? is the JS queue growing?
  // (pumpTimerCb appends; the node's onaudioprocess drains.)
  const bridge = await page.evaluate(() => {
    const out = { hasSDL2: !!window.Module?.['SDL2'] };
    try {
      const bq = window.Module?.['SDL2']?.bwQ;
      out.hasBwQ = !!bq;
      out.qLen = bq?.q?.length || 0;
      out.hasNode = !!bq?.node;
      out.nodeBuf = bq?.node?.bufferSize || 0;
    } catch (e) { out.err = String(e).slice(0, 120); }
    return out;
  });
  await page.waitForTimeout(3000);
  const bridge2 = await page.evaluate(() => {
    try {
      const bq = window.Module?.['SDL2']?.bwQ;
      return { qLen: bq?.q?.length || 0 };
    } catch (e) { return { err: String(e).slice(0, 80) }; }
  });
  // Per-tap peaks across ALL taps (taps[0]=SDL driver node, taps[1]=our bridge node)
  const tapPeaks = await page.evaluate(() => {
    const res = [];
    for (const t of (window.__bwAudio?.taps || [])) {
      let peak = 0, n = 0;
      for (const c of t.chunks) { n += c.data.length; for (let i = 0; i < c.data.length; i += 8) { const a = Math.abs(c.data[i]); if (a > peak) peak = a; } }
      res.push({ chunks: t.chunks.length, samples: n, peak });
    }
    return res;
  });
  console.log('BRIDGE', JSON.stringify({ bridge, bridge2, tapPeaks }));
  summary.bridge = { bridge, bridge2, tapPeaks };
  const secondFailedOpen = logs.slice(startIdx2).some(l => /Failed to open audio/.test(l));
  const secondOpenAudio = logs.slice(startIdx2).some(l => /openAudio: freq/.test(l));
  console.log('SECOND', JSON.stringify({ tonestart: !!ts2, failedOpen: secondFailedOpen, openAudio: secondOpenAudio }));
  await page.waitForTimeout(8000);
  await flush();

  const snap = await page.evaluate(() => {
    const a = window.__bwAudio;
    let tapPeak = 0, tapN = 0;
    for (const t of (a.taps || [])) {
      for (const c of t.chunks) {
        tapN += c.data.length;
        for (let i = 0; i < c.data.length; i += 4) {
          const v = Math.abs(c.data[i]);
          if (v > tapPeak) tapPeak = v;
        }
      }
    }
    return {
      ctxState: a.ctx?.state || 'none',
      nodeRecs: a.nodeRecs,
      events: a.events,
      setIntervals: a.setIntervals,
      tapPeak, tapN,
      tapChunks: (a.taps || []).map(t => t.chunks.length),
    };
  });
  summary.diag = snap;
  summary.diag.secondSpawn = { tonestart: !!ts2, failedOpen: secondFailedOpen, openAudio: secondOpenAudio };
  summary.errors = errors.slice(0, 20);
  summary.openAudioLines = logs.filter(l => /openAudio:/.test(l)).length;
  summary.failedOpenLines = logs.filter(l => /Failed to open audio/.test(l)).length;
  summary.tonestartSeen = !!ts;
  await writeFile(`${dir}/diag.json`, JSON.stringify(summary, null, 2));
  await flush();
  console.log('DIAG', JSON.stringify({ nodeRecs: snap.nodeRecs, tapPeak: snap.tapPeak, tapN: snap.tapN, ctx: snap.ctxState, openAudio: summary.openAudioLines, failedOpen: summary.failedOpenLines, pageerrors: errors.length }));
  console.log('EVENTS', JSON.stringify(snap.events));
  console.log('INTERVALS', JSON.stringify(snap.setIntervals.slice(0, 10)));
} finally {
  await browser.close();
  server.close();
  await slot.release();
}
console.log('DONE', tag);
