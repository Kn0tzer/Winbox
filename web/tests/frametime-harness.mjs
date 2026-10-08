// Frame-time measurement harness for Boxedwine64.
// MEASUREMENT ONLY — installs page-side instrumentation, changes no emulator code.
//
// Page-side instrumentation via performance.now():
//   - window.bwFt.guest: timestamp when the v2 page tier finishes assembling a
//     guest frame (last chunk of a frame, MORE flag clear). This is the
//     guest-CPU frame pacing: guest render + bridge hop + chunk transfer.
//   - window.bwFt.page: t0/t1/dt around renderer2.render() — the page-GPU
//     submit time for one presented frame (WebGPU work incl. SwiftShader).
//   - latency: page render completion vs the guest frame it consumed.
//
// Scenarios:
//   SCENARIO=pong   D3D9 Pong (dxvk-d3d9-64 path, continuous game). Warms up
//                   WARMUP_FRAMES guest frames, then measures MEASURE_FRAMES.
//   SCENARIO=tri11  D3D11 tri11_93.exe (one offscreen frame per run). Repeats
//                   RUNS spawns; one sample per run.
//
// Output: test-results/frametime-<tag>/{console.log, frametimes.json, summary.json}
// summary.json carries mean/median/p95 for guest/page/latency plus the exact
// build served (wasm mtime+sha256, BUILD_INFO.json, vkwebgpu.mjs mtime+sha256,
// git sha + dirty count) so a future optimization lane can re-run apples-to-apples.
//
// Run from repo root:
//   TAG=frametime-pong1 SCENARIO=pong node web/tests/frametime-harness.mjs
//   TAG=frametime-tri11a SCENARIO=tri11 RUNS=5 node web/tests/frametime-harness.mjs
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { mkdir, writeFile, readFile, stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { execSync } from 'node:child_process';

const ROOT = '/home/ubuntu/boxedwine64';
const scenario = process.env.SCENARIO || 'pong';
if (!['pong', 'tri11'].includes(scenario)) throw new Error('SCENARIO must be pong or tri11');
const tag = process.env.TAG || `frametime-${scenario}-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/frametime-${tag}`;
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
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

const SCEN = {
  pong: {
    urlParams: 'p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0',
    assets: [['tri9pong.exe', 'tools/dxvk/tri9/tri9pong.exe'],
             ['d3d9.dll', 'tools/dxvk/build.w64/src/d3d9/d3d9.dll'],
             ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1']],
    spawnArgv: '/usr/lib/wine/wine64\nZ:\\home\\username\\tri9pong.exe',
    spawnEnv: 'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username',
  },
  tri11: {
    urlParams: 'p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=0&persist=0&gltrace=0',
    assets: [['tri11_93.exe', 'tools/d3dtests/tri11_93.exe'],
             ['d3d11.dll', 'tools/dxvk/build.w64/src/d3d11/d3d11.dll'],
             ['dxgi.dll', 'tools/dxvk/build.w64/src/dxgi/dxgi.dll'],
             ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1']],
    spawnArgv: '/usr/lib/wine/wine64\nZ:\\home\\username\\tri11_93.exe',
    spawnEnv: 'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d11=n;dxgi=n\nLD_LIBRARY_PATH=/home/username',
  },
}[scenario];

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
    try { v = await fn(); } catch (e) { /* page may be busy; retry */ }
    if (v) return true;
    if (Date.now() > until) throw new Error(`timeout waiting for ${label}`);
    await page.waitForTimeout(1000);
  }
}

async function buildIdentity() {
  const wasmPath = `${ROOT}/dist/runtime/boxedwine64.wasm`;
  const wasmBuf = await readFile(wasmPath);
  const wasmStat = await stat(wasmPath);
  const vkPath = `${ROOT}/dist/vkwebgpu.mjs`;
  const vkBuf = await readFile(vkPath);
  const vkStat = await stat(vkPath);
  const sha = (b) => createHash('sha256').update(b).digest('hex');
  let buildInfo = null;
  try { buildInfo = JSON.parse(await readFile(`${ROOT}/dist/runtime/BUILD_INFO.json`, 'utf8')); } catch {}
  let gitSha = null, dirtyCount = null;
  try {
    gitSha = execSync('git rev-parse HEAD', { cwd: ROOT }).toString().trim();
    dirtyCount = execSync('git status --porcelain', { cwd: ROOT }).toString().trim().split('\n').filter(Boolean).length;
  } catch {}
  return {
    wasm: { path: 'dist/runtime/boxedwine64.wasm', mtimeUtc: new Date(wasmStat.mtimeMs).toISOString(),
            bytes: wasmBuf.length, sha256: sha(wasmBuf) },
    vkwebgpu: { path: 'dist/vkwebgpu.mjs', mtimeUtc: new Date(vkStat.mtimeMs).toISOString(),
                bytes: vkBuf.length, sha256: sha(vkBuf) },
    buildInfo, gitSha, gitDirtyCount: dirtyCount,
  };
}

let summary = { scenario, tag, measured: false };
try {
  const sc = SCEN;
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

  // Bootstrap: require the vkboot sentinel (no timeout skip — spawning into a
  // half-booted prefix loses the spawn). The sentinel can arrive between the
  // last in-loop flush and the match, so flush once more after the loop.
  const bootstrapUntil = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) throw new Error('Wine bootstrap sentinel timed out');
    await flush();
    await page.waitForTimeout(500);
  }
  await flush();
  console.log('BOOTSTRAP COMPLETE');

  // Install timing harness BEFORE the vk tier exists: a setter trap on
  // window.bwVkRenderer2 patches renderer.render the moment the tier creates
  // it, so no frame's page-GPU time is missed (no emulator code touched).
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
  // Guest frame completions: the authoritative MORE bit lives in the chunk
  // header bytes, NOT the EM_ASM flags argument (the bridge always passes 0:
  // hopV2LastToPage passes (U32)0; the tier itself does `void flags` and
  // re-parses the header). v2 wire: 16 B header, u16 flags LE at offset 6,
  // bit0 = MORE. A MORE=0 chunk completes one assembled frame.
  // Verified post-hoc against stats().v2.framesReceived (myCounterMatchesStats).
  await page.evaluate(() => {
    const tier = window.bwVkTier;
    const _chunk = tier.chunk.bind(tier);
    let g = 0;
    tier.chunk = (bytes, flags) => {
      const r = _chunk(bytes, flags);
      let more = 0;
      try { if (bytes && bytes.length >= 8) more = (bytes[6] | (bytes[7] << 8)) & 1; } catch {}
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

  if (scenario === 'pong') {
    const WARMUP = Number(process.env.WARMUP_FRAMES || 60);
    const MEASURE = Number(process.env.MEASURE_FRAMES || 300);
    const exeName = 'tri9pong';
    console.log('SPAWN', await spawn());
    // Fail fast if the guest never picks up the spawn (lost spawn symptom).
    await pollUntil(() => logs.some(l => new RegExp(`spawning.*${exeName}`).test(l)),
                    180000, `guest spawn of ${exeName}`);
    console.log('SPAWN CONFIRMED IN GUEST');
    await pollUntil(async () => (await litPixels()) > 1000, 600000, 'first lit frame');
    console.log('LIT', await litPixels());
    await pollUntil(async () => (await ftLens()).g >= WARMUP, 600000, `warmup ${WARMUP} guest frames`);
    console.log('WARMUP DONE');
    let earlyExit = null;
    try {
      await pollUntil(async () => {
        if (logs.some(l => /\bRESULT [01]\b/.test(l))) { earlyExit = true; return true; }
        return (await ftLens()).g >= WARMUP + MEASURE;
      }, 1200000, `measure ${MEASURE} guest frames`);
    } catch (e) {
      if (!earlyExit) throw e;
    }
    await flush();
    const data = await page.evaluate(() => {
      // Extract primitives only: the full stats object carries renderer
      // snapshots that may not survive structured clone.
      let v2 = null, lit = 0, err = null;
      try {
        const st = window.bwVkStats();
        v2 = { framesReceived: st.v2.framesReceived, rendered: st.v2.rendered,
               droppedConsumer: st.v2.droppedConsumer, chunksReceived: st.v2.chunksReceived,
               chunkGaps: st.v2.chunkGaps, rejected: st.v2.rejected };
        lit = st.litPixels || 0;
      } catch (e) { err = String(e && e.message || e); }
      return { ft: window.bwFt, v2, lit, err };
    });
    await writeFile(`${dir}/frametimes.json`, JSON.stringify(data.ft));

    const guestsAll = data.ft.guest;
    const guests = guestsAll.slice(WARMUP); // measured window (post-warmup)
    if (guests.length < 120) throw new Error(`too few measured guest frames: ${guests.length}`);
    const gDts = diff(guests.map(g => g.t));
    const t0 = guests[0].t, t1 = guests[guests.length - 1].t;
    const pages = data.ft.page.filter(p => p.rendered && p.t0 >= t0 - 1000 && p.t0 <= t1 + 1000);
    const pDts = pages.map(p => p.dt);
    const lats = [];
    for (const p of pages) {
      for (let i = guestsAll.length - 1; i >= 0; i--) {
        if (guestsAll[i].t <= p.t0) { lats.push(p.t1 - guestsAll[i].t); break; }
      }
    }
    summary = {
      scenario, tag, measured: true, dateUtc: new Date().toISOString(),
      method: 'page-side performance.now(); guest frame = v2 frame assembly complete (MORE=0 chunk); page frame = renderer2.render() wall dt; warmup excluded',
      window: { warmupFrames: WARMUP, measuredGuestFrames: guests.length,
                wallSeconds: (t1 - t0) / 1000, earlyExit: !!earlyExit },
      counts: { guestFramesTotal: guestsAll.length, pageRendersTotal: data.ft.page.length,
                pageRendersInWindow: pages.length,
                statsFramesReceived: data.v2?.framesReceived ?? null,
                statsRendered: data.v2?.rendered ?? null,
                myCounterMatchesStats: data.v2 ? (guestsAll.length === data.v2.framesReceived) : null,
                droppedConsumer: data.v2?.droppedConsumer ?? null },
      guestFrameMs: statsOf(gDts),
      guestFps: gDts.length ? 1000 / (gDts.reduce((a, b) => a + b, 0) / gDts.length) : null,
      pageRenderMs: statsOf(pDts),
      presentLatencyMs: statsOf(lats),
      litPixels: await litPixels(),
      build,
      errors,
    };
    console.log('PONG SUMMARY', JSON.stringify({ guest: summary.guestFrameMs, page: summary.pageRenderMs,
      lat: summary.presentLatencyMs, fps: summary.guestFps }));
  } else {
    // tri11: one offscreen frame per spawn; repeat RUNS times.
    const RUNS = Number(process.env.RUNS || 5);
    const runs = [];
    for (let i = 0; i < RUNS; i++) {
      const mark = await page.evaluate(() => ({ g: window.bwFt.guest.length, p: window.bwFt.page.length,
                                                t: Date.now(), logs: 0 }));
      const logsBefore = logs.length;
      const tSpawn = Date.now();
      console.log(`RUN ${i} SPAWN`, await spawn());
      await pollUntil(() => logs.slice(logsBefore).some(l => /spawning.*tri11_93/.test(l)),
                      180000, `tri11 run ${i} guest spawn`);
      await pollUntil(() => logs.slice(logsBefore).some(l => /tri1193: RESULT [01]/.test(l)),
                      600000, `tri11 run ${i} RESULT`);
      const tResult = Date.now();
      // The guest prints RESULT after readback; the v2 chunk + page render can
      // trail it by a moment — wait for the instrumentation to see the frame.
      await pollUntil(async () => (await ftLens()).g > mark.g, 30000,
                      `tri11 run ${i} guest frame`).catch(e => console.log('WARN', e.message));
      await pollUntil(async () => (await ftLens()).p > mark.p, 60000,
                      `tri11 run ${i} page render`).catch(e => console.log('WARN', e.message));
      const after = await page.evaluate((m) => ({
        guest: window.bwFt.guest.slice(m.g), page: window.bwFt.page.slice(m.p),
        lit: (() => { try { return window.bwVkStats?.().litPixels || 0; } catch { return 0; } })(),
      }), mark);
      const resultLine = logs.slice(logsBefore).find(l => /tri1193: RESULT [01]/.test(l));
      const litLine = logs.slice(logsBefore).find(l => /tri1193: LIT=/.test(l));
      const g = after.guest[0], p = after.page[0];
      runs.push({
        run: i,
        guestFrames: after.guest.length,
        pageRenders: after.page.length,
        guestFrameT: g ? g.t : null,
        pageRenderMs: p ? p.dt : null,
        pageRendered: p ? p.rendered : null,
        // guest->page latency: render completion vs guest frame completion (same-tick clock)
        guestToPageDoneMs: (g && p) ? (p.t1 - g.t) : null,
        spawnToResultMs: tResult - tSpawn,
        lit: after.lit, resultLine: resultLine || null, litLine: litLine || null,
      });
      console.log(`RUN ${i} DONE`, JSON.stringify(runs[i]));
    }
    const data = await page.evaluate(() => {
      let v2 = null, err = null;
      try {
        const st = window.bwVkStats();
        v2 = { framesReceived: st.v2.framesReceived, rendered: st.v2.rendered,
               droppedConsumer: st.v2.droppedConsumer };
      } catch (e) { err = String(e && e.message || e); }
      return { ft: window.bwFt, v2, err };
    });
    await writeFile(`${dir}/frametimes.json`, JSON.stringify(data.ft));
    const ok = runs.filter(r => r.pageRenderMs !== null);
    summary = {
      scenario, tag, measured: ok.length > 0, dateUtc: new Date().toISOString(),
      method: 'per-spawn single offscreen frame; pageRenderMs = renderer2.render() wall dt; guestToPageDoneMs = render completion - guest frame assembly (same performance.now clock)',
      runs,
      pageRenderMs: statsOf(ok.map(r => r.pageRenderMs)),
      guestToPageDoneMs: statsOf(ok.map(r => r.guestToPageDoneMs).filter(x => x !== null)),
      spawnToResultMs: statsOf(runs.map(r => r.spawnToResultMs)),
      counts: { guestFramesTotal: data.ft.guest.length, pageRendersTotal: data.ft.page.length,
                statsFramesReceived: data.v2?.framesReceived ?? null,
                statsRendered: data.v2?.rendered ?? null },
      build,
      errors,
    };
    console.log('TRI11 SUMMARY', JSON.stringify({ page: summary.pageRenderMs, lat: summary.guestToPageDoneMs }));
  }
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
