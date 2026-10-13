// Frame-time PROFILER probe for Boxedwine64 (D3D9 Pong).
// Pairs with the additive, env-gated instrumentation:
//   BW64_VKPROF=1            vk64bridge.cpp wall timers -> "vk64: VKPROF ..." klog/frame
//   BW64_RIPSAMPLE_PROFILE=1 ripSampler.cpp+cpu64.cpp insn histogram -> "RIPPROF ..." klog/frame
// Served from dist-profile/ (WINE_DIST_DIR) — an instrumented build kept separate
// from the shared dist/. Page-side work here is measurement only.
//
// Page-side (performance.now()):
//   - guest frame completions: MORE=0 chunk arrival (same as frametime-harness)
//   - per-chunk page-tier decode time: wall dt around tier.chunk()
// Wasm-side (parsed from console.log klog lines):
//   - VKPROF: wall/submit/bufdata/imgdata/trapOther/present/hop/guest per frame
//   - RIPPROF: per-frame instruction histogram by module basename
//
// Run from repo root:
//   TAG=frametime-profile-pong1 node web/tests/frametime-profile.mjs
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { mkdir, writeFile, readFile, stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { execSync } from 'node:child_process';

const ROOT = '/home/ubuntu/boxedwine64';
const tag = process.env.TAG || `frametime-profile-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/frametime-profile-${tag}`;
await mkdir(dir, { recursive: true });
const distDir = process.env.WINE_DIST_DIR || 'dist-profile';

// Fail fast if the instrumented dist is absent.
try { await stat(`${ROOT}/${distDir}/runtime/boxedwine64.wasm`); }
catch { console.log('HARNESS ERROR missing instrumented wasm at', `${ROOT}/${distDir}/runtime/boxedwine64.wasm`); process.exitCode = 1; throw new Error('no dist-profile'); }

const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
const server = await serve(distDir);
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
page.on('pageerror', e => errors.push(String(e && e.message || e)));
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const timeout = Number(process.env.TIMEOUT || 1800000);

const sc = {
  urlParams: 'p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0&vkprof=1&ripsampleprof=1',
  assets: [['tri9pong.exe', 'tools/dxvk/tri9/tri9pong.exe'],
           ['d3d9.dll', 'tools/dxvk/build.w64/src/d3d9/d3d9.dll'],
           ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1']],
  spawnArgv: '/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\tri9pong.exe',
  // NOTE: wasm-side profiling is enabled via URL params (vkprof=1,
  // ripsampleprof=1 -> runtime ENV in wine64-launcher.js). spawnEnv only
  // reaches the GUEST process environment, not the wasm runtime.
  spawnEnv: 'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username',
};

const statsOf = (xs) => {
  if (!xs.length) return { n: 0 };
  const s = [...xs].sort((a, b) => a - b);
  const mean = s.reduce((a, b) => a + b, 0) / s.length;
  const q = (p) => s[Math.min(s.length - 1, Math.floor(p * s.length))];
  const variance = s.reduce((a, b) => a + (b - mean) ** 2, 0) / s.length;
  return { n: s.length, mean, median: q(0.5), p95: q(0.95), p99: q(0.99),
           min: s[0], max: s[s.length - 1], std: Math.sqrt(variance) };
};
const diff = (ts) => ts.slice(1).map((t, i) => t - ts[i]);

async function pollUntil(fn, ms, label) {
  const until = Date.now() + ms;
  for (;;) {
    let v = false;
    try { v = await fn(); } catch (e) {}
    if (v) return true;
    if (Date.now() > until) throw new Error(`timeout waiting for ${label}`);
    await page.waitForTimeout(1000);
  }
}

async function buildIdentity() {
  const wasmPath = `${ROOT}/${distDir}/runtime/boxedwine64.wasm`;
  const wasmBuf = await readFile(wasmPath);
  const wasmStat = await stat(wasmPath);
  const vkPath = `${ROOT}/${distDir}/vkwebgpu.mjs`;
  const vkBuf = await readFile(vkPath);
  const vkStat = await stat(vkPath);
  const sha = (b) => createHash('sha256').update(b).digest('hex');
  let buildInfo = null;
  try { buildInfo = JSON.parse(await readFile(`${ROOT}/${distDir}/runtime/BUILD_INFO.json`, 'utf8')); } catch {}
  let gitSha = null, dirtyCount = null;
  try {
    gitSha = execSync('git rev-parse HEAD', { cwd: ROOT }).toString().trim();
    dirtyCount = execSync('git status --porcelain', { cwd: ROOT }).toString().trim().split('\n').filter(Boolean).length;
  } catch {}
  return {
    distDir,
    wasm: { path: `${distDir}/runtime/boxedwine64.wasm`, mtimeUtc: new Date(wasmStat.mtimeMs).toISOString(),
            bytes: wasmBuf.length, sha256: sha(wasmBuf) },
    vkwebgpu: { path: `${distDir}/vkwebgpu.mjs`, mtimeUtc: new Date(vkStat.mtimeMs).toISOString(),
                bytes: vkBuf.length, sha256: sha(vkBuf) },
    buildInfo, gitSha, gitDirtyCount: dirtyCount,
  };
}

let summary = { tag, measured: false };
try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?${sc.urlParams}`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  for (const [name, rel] of sc.assets) {
    const bytes = await readFile(`${ROOT}/${rel}`);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const registered = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
      if (registered !== 1) throw new Error('Guest VFS registration failed for ' + name);
    }, { name, b64 });
  }
  console.log('ASSETS STAGED', sc.assets.length);

  const bootstrapUntil = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) throw new Error('Wine bootstrap sentinel timed out');
    await flush();
    await page.waitForTimeout(500);
  }
  await flush();
  console.log('BOOTSTRAP COMPLETE');

  await page.evaluate(() => {
    window.bwFt = { guest: [], page: [] };
    let r2 = null;
    window.__ftPatchRender = (renderer) => {
      if (!renderer || renderer.__ftPatched) return;
      renderer.__ftPatched = true;
      const _render = renderer.render.bind(renderer);
      let n = 0;
      renderer.render = async (f) => {
        const t0 = performance.now();
        const res = await _render(f);
        const t1 = performance.now();
        n++;
        window.bwFt.page.push({ n, t0, t1, dt: t1 - t0, rendered: !!(res && res.rendered) });
        return res;
      };
    };
    Object.defineProperty(window, 'bwVkRenderer2', {
      configurable: true,
      get() { return r2; },
      set(v) { r2 = v; if (v) window.__ftPatchRender(v); },
    });
  });
  await page.evaluate(async () => {
    const { installVkPageTier } = await import('../vkwebgpu.mjs');
    window.bwVkTier = installVkPageTier();
  });
  // Chunk hook with page-tier decode timing: decMs isolates the page-side
  // record decode from the wasm-side EM_ASM hop + JS memcpy (slice).
  await page.evaluate(() => {
    const tier = window.bwVkTier;
    const _chunk = tier.chunk.bind(tier);
    let g = 0;
    window.bwFtChunks = [];
    tier.chunk = (bytes, flags) => {
      const c0 = performance.now();
      const r = _chunk(bytes, flags);
      const c1 = performance.now();
      let more = 0;
      try { if (bytes && bytes.length >= 8) more = (bytes[6] | (bytes[7] << 8)) & 1; } catch {}
      window.bwFtChunks.push({ bytes: bytes ? bytes.length : 0, more, decMs: c1 - c0 });
      if (!more) { g++; window.bwFt.guest.push({ n: g, t: performance.now() }); }
      return r;
    };
    window.__ftChunkPatched = true;
  });
  console.log('TIMING HOOKS INSTALLED');

  const spawn = () => page.evaluate(([argv, env]) =>
    window.bwRuntime.call('bw64_spawn', ['string', 'string'], [argv, env]),
    [sc.spawnArgv, sc.spawnEnv]);
  const ftLens = () => page.evaluate(() => ({ g: window.bwFt.guest.length, p: window.bwFt.page.length }));
  const litPixels = () => page.evaluate(() => { try { return window.bwVkStats?.().litPixels || 0; } catch { return 0; } });

  const build = await buildIdentity();

  const WARMUP = Number(process.env.WARMUP_FRAMES || 60);
  const MEASURE = Number(process.env.MEASURE_FRAMES || 150);
  console.log('SPAWN', await spawn());
  await pollUntil(() => logs.some(l => /spawning.*tri9pong/.test(l)), 180000, 'guest spawn of tri9pong');
  console.log('SPAWN CONFIRMED IN GUEST');
  await pollUntil(async () => (await litPixels()) > 1000, 600000, 'first lit frame');
  console.log('LIT', await litPixels());
  await pollUntil(async () => (await ftLens()).g >= WARMUP, 600000, `warmup ${WARMUP} guest frames`);
  console.log('WARMUP DONE');
  await pollUntil(async () => (await ftLens()).g >= WARMUP + MEASURE, 1200000, `measure ${MEASURE} guest frames`);
  await flush();

  const data = await page.evaluate(() => {
    let v2 = null, lit = 0, err = null;
    try {
      const st = window.bwVkStats();
      v2 = { framesReceived: st.v2.framesReceived, rendered: st.v2.rendered,
             droppedConsumer: st.v2.droppedConsumer, chunksReceived: st.v2.chunksReceived,
             chunkGaps: st.v2.chunkGaps, rejected: st.v2.rejected };
      lit = st.litPixels || 0;
    } catch (e) { err = String(e && e.message || e); }
    return { ft: window.bwFt, chunks: window.bwFtChunks, v2, lit, err };
  });
  await writeFile(`${dir}/frametimes.json`, JSON.stringify({ ft: data.ft, chunks: data.chunks }));

  // ---- Parse wasm-side profiler lines out of console.log ----
  const vkRe = /VKPROF frame=(\d+) wall=([\d.]+)ms submit=([\d.]+)ms bufdata=([\d.]+)ms bufB=(\d+)\/(\d+) imgdata=([\d.]+)ms imgB=(\d+)\/(\d+) trapOther=([\d.]+)ms\((\d+) calls\) present=([\d.]+)ms hop=([\d.]+)ms hopB=(\d+)ch\/(\d+)B guest=([\d.]+)ms bufC=(\d+) imgC=(\d+) ser2=([\d.]+)ms queue=([\d.]+)ms topT=(\S+)/;
  const ripTotRe = /RIPPROF pid=(\d+) totalInsns=(\d+)/;
  const ripModRe = /RIPPROF pid=(\d+) mod=(\S+) insns=(\d+) pct=([\d.]+)/;
  const frames = [];
  let cur = null;
  let vkprofEnabledSeen = false;
  for (const line of logs) {
    if (/VKPROF enabled/.test(line)) { vkprofEnabledSeen = true; continue; }
    const m = line.match(vkRe);
    if (m) {
      cur = { frame: +m[1], wall: +m[2], submit: +m[3], bufdata: +m[4],
              bufHashed: +m[5], bufSent: +m[6], imgdata: +m[7],
              imgHashed: +m[8], imgSent: +m[9], trapOther: +m[10], trapCalls: +m[11],
              present: +m[12], hop: +m[13], hopChunks: +m[14], hopBytes: +m[15],
              guest: +m[16], bufC: +m[17], imgC: +m[18], ser2: +m[19], queue: +m[20],
              topT: m[21], mods: {} };
      frames.push(cur);
      continue;
    }
    const tm = line.match(ripTotRe);
    if (tm && cur) { cur.ripPid = +tm[1]; cur.ripTotal = +tm[2]; continue; }
    const mm = line.match(ripModRe);
    if (mm && cur) { cur.mods[mm[2]] = { insns: +mm[3], pct: +mm[4] }; continue; }
  }

  const guestsAll = data.ft.guest;
  const guests = guestsAll.slice(WARMUP);
  if (guests.length < 60) throw new Error(`too few measured guest frames: ${guests.length}`);
  const gDts = diff(guests.map(g => g.t));
  // Page-side decode per measured frame: attribute chunks to frames via MORE bit.
  const chunks = data.chunks;
  const frameDec = [];
  let acc = 0, accB = 0, nCh = 0;
  for (const c of chunks) { acc += c.decMs; accB += c.bytes; nCh++; if (!c.more) { frameDec.push({ decMs: acc, bytes: accB, chunks: nCh }); acc = 0; accB = 0; nCh = 0; } }
  const frameDecM = frameDec.slice(WARMUP, WARMUP + guests.length);

  const pick = (k) => statsOf(frames.map(f => f[k]).filter(x => typeof x === 'number'));
  const modNames = [...new Set(frames.flatMap(f => Object.keys(f.mods)))];
  const modStats = {};
  for (const mn of modNames) modStats[mn] = statsOf(frames.map(f => f.mods[mn] ? f.mods[mn].pct : 0));

  summary = {
    tag, measured: true, dateUtc: new Date().toISOString(),
    distDir,
    method: 'wasm-side VKPROF wall timers (present-exit to present-exit) + RIPPROF insn histogram by module per frame; page-side MORE=0 chunk arrivals + tier.chunk decode dt; warmup excluded',
    env: { urlParams: 'vkprof=1&ripsampleprof=1 -> runtime ENV', note: 'spawnEnv cannot reach the wasm runtime' },
    window: { warmupFrames: WARMUP, measuredGuestFrames: guests.length, vkprofFrames: frames.length,
              vkprofEnabledSeen, ripprofFrames: frames.filter(f => f.ripTotal > 0).length },
    counts: { guestFramesTotal: guestsAll.length,
              statsFramesReceived: data.v2?.framesReceived ?? null,
              myCounterMatchesStats: data.v2 ? (guestsAll.length === data.v2.framesReceived) : null,
              droppedConsumer: data.v2?.droppedConsumer ?? null },
    // wasm-side breakdown (ms/frame)
    wasm: {
      wallMs: pick('wall'), submitMs: pick('submit'), bufdataMs: pick('bufdata'), imgdataMs: pick('imgdata'),
      trapOtherMs: pick('trapOther'), trapCalls: pick('trapCalls'), presentMs: pick('present'),
      hopMs: pick('hop'), hopChunks: pick('hopChunks'), hopBytes: pick('hopBytes'),
      guestMs: pick('guest'),
      bufC: pick('bufC'), imgC: pick('imgC'), ser2Ms: pick('ser2'), queueMs: pick('queue'),
      bufHashedB: pick('bufHashed'), bufSentB: pick('bufSent'),
      imgHashedB: pick('imgHashed'), imgSentB: pick('imgSent'),
      ripTotalInsns: pick('ripTotal'),
    },
    // page-side (ms/frame)
    page: {
      guestFrameMs: statsOf(gDts),
      guestFps: gDts.length ? 1000 / (gDts.reduce((a, b) => a + b, 0) / gDts.length) : null,
      chunkDecodeMs: statsOf(frameDecM.map(f => f.decMs)),
      chunkBytes: statsOf(frameDecM.map(f => f.bytes)),
      chunksPerFrame: statsOf(frameDecM.map(f => f.chunks)),
      pageRenderMs: statsOf(data.ft.page.filter(p => p.rendered).map(p => p.dt)),
    },
    modulesPct: modStats,
    litPixels: await litPixels(),
    build,
    errors,
  };
  await writeFile(`${dir}/breakdown.json`, JSON.stringify({ frames, modStats }, null, 1));
  console.log('PROFILE SUMMARY', JSON.stringify({
    vkprofFrames: frames.length, wall: summary.wasm.wallMs.mean?.toFixed(1),
    guest: summary.wasm.guestMs.mean?.toFixed(1), submit: summary.wasm.submitMs.mean?.toFixed(1),
    hop: summary.wasm.hopMs.mean?.toFixed(1), pageGuest: summary.page.guestFrameMs.mean?.toFixed(1),
    lit: summary.litPixels,
  }));
} catch (e) {
  console.log('HARNESS ERROR', e.message);
  summary.error = String(e.stack || e);
} finally {
  await flush();
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('SUMMARY WRITTEN', `${dir}/summary.json`, 'measured=' + summary.measured);
  await slot.release();
  await browser.close();
  server.close();
  if (!summary.measured) process.exitCode = 1;
}
console.log('DONE slot=' + slot.index);
