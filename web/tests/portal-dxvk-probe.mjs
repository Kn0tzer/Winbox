// Portal via DXVK lane: gamezip (remote Portal) + local overlay zip carrying
// 32-bit DXVK d3d9.dll + fixed libvk32 shim at home/username/apps/.
// URL: dlloverrides=d3d9=n (native DXVK from overlay app dir),
//      ldpath=/home/username/apps (guest linker finds overlay libvulkan.so.1).
// Pass bar: vk32 traps appear (Vulkan lane active), then litPixels > 500.
// Also records FPS from the bwVkStats rendered-frame counter delta.
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { mkdir, writeFile, stat } from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { execSync } from 'node:child_process';

const repo = '/home/ubuntu/boxedwine64';
const tag = process.env.TAG || `portal-dxvk-${Date.now()}`;
const dir = `${repo}/test-results/${tag}`;
await mkdir(dir, { recursive: true });

// Build the overlay zip fresh (DXVK 32-bit d3d9 + fixed 32-bit shim).
// d3d9.dll goes BOTH at apps/ root (hl2.exe's dir) AND apps/bin/ (Source's
// shaderapidx9.dll imports d3d9.dll and only finds it via its module dir).
const overlayZip = `${dir}/dxvk32-overlay.zip`;
execSync(
  `rm -rf /tmp/dxvk32ovl && mkdir -p /tmp/dxvk32ovl/home/username/apps/bin` +
  ` && cp ${repo}/tools/dxvk/build.w32/src/d3d9/d3d9.dll /tmp/dxvk32ovl/home/username/apps/` +
  ` && cp /tmp/dxvk32ovl/home/username/apps/d3d9.dll /tmp/dxvk32ovl/home/username/apps/bin/` +
  ` && cp ${repo}/tools/rootfs64/libvk32/libvulkan.so.1 /tmp/dxvk32ovl/home/username/apps/` +
  ` && (cd /tmp/dxvk32ovl && zip -q ${overlayZip} home/username/apps/d3d9.dll home/username/apps/bin/d3d9.dll home/username/apps/libvulkan.so.1)` +
  ` && rm -rf /tmp/dxvk32ovl && unzip -l ${overlayZip}`,
  { stdio: 'inherit' });
const overlaySize = (await stat(overlayZip)).size;

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);

let browser, browserServer, server;
const logs = [];
const presents = [];
try {
  server = await serve(repo);
  const port = server.address().port;
  browserServer = await chromium.launchServer({
    executablePath: '/usr/local/bin/chromium',
    headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
      '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
      '--enable-features=Vulkan'],
  });
  browser = await chromium.connect(browserServer.wsEndpoint());
  await writeFile(`/tmp/${tag}-browser.ws`, browserServer.wsEndpoint());
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  await page.addInitScript(() => {
    let renderer;
    globalThis.__portalShaderFailures = [];
    Object.defineProperty(globalThis, 'bwVkRenderer2', {
      configurable: true,
      get: () => renderer,
      set(value) {
        renderer = value;
        const render = value.render.bind(value);
        globalThis.__portalFrames = [];
        value.render = async frame => {
          globalThis.__portalLastFrame = frame;
          globalThis.__portalFrames.push({ id: frame.frame, width: frame.width, height: frame.height,
            passes: frame.passes.map(p => ({ w: p.w, h: p.h, attachments: p.attachments,
              ops: p.ops.map(o => o.op) })),
            images: [...frame.images].map(([id, image]) => ({ id, w: image.w, h: image.h,
              format: image.format, bytes: image.pixels?.length || 0 })) });
          if (globalThis.__portalFrames.length > 8) globalThis.__portalFrames.shift();
          return render(frame);
        };
        const original = value.shaders.get.bind(value.shaders);
        value.shaders.__portalWrapped = true;
        value.shaders.get = async (stage, mod, opts) => {
          const result = await original(stage, mod, opts);
          if (!result && mod && !globalThis.__portalShaderFailures.some(f => f.hash === mod.hash)) {
            let binary = '';
            for (const byte of mod.code) binary += String.fromCharCode(byte);
            globalThis.__portalShaderFailures.push({ stage, hash: mod.hash, size: mod.size,
              bytes: btoa(binary), opts: { ...opts, specOverrides: (opts?.specOverrides || [])
                .map(e => ({ constantID: e.constantID, value: Array.from(e.value) })) } });
          }
          return result;
        };
      },
    });
  });
  if (process.env.PROFILE_GUEST === '1') await page.addInitScript(() => {
    let environment;
    Object.defineProperty(globalThis, 'ENV', {
      configurable: true, get: () => environment,
      set(value) {
        environment = value;
        Object.assign(value, { BW64_RIPSAMPLE: '1', BW64_RIPSAMPLE_HB: '1', BW64_RIPSAMPLE_STACK: '1' });
      },
    });
  });
  const logFile = createWriteStream(`${dir}/console.log`);
  page.on('console', m => {
    const l = `${m.type()}: ${m.text()}`;
    logs.push(l); logFile.write(l + '\n');
    if (/vk32: vkQueuePresentKHR swapchain=/.test(l)) presents.push(performance.now());
  });
  page.on('pageerror', e => { const l = `pageerror: ${e.message}`; logs.push(l); logFile.write(l + '\n'); });

  const params = new URLSearchParams({
    gamezip: 'https://kn0tzer.work.gd/games/portal.zip',
    gamezipsize: '3349436598',
    overlay: `http://127.0.0.1:${port}/test-results/${tag}/dxvk32-overlay.zip`,
    overlaysize: String(overlaySize),
    exe: 'hl2.exe',
    args: process.env.PORTAL_ARGS || JSON.stringify(['-game', 'portal', '-novid', '-windowed']),
    arch: 'x86',
    session: '0',
    fresh: '1',
    dlloverrides: 'd3d9=n',
    ldpath: '/home/username/apps',
  });
  await page.goto(`http://127.0.0.1:${port}/dist/runtime/index.html?${params}`, { timeout: 60000 });
  console.log('NAVIGATED overlay=' + overlaySize);

  const getStats = () => page.evaluate(() => {
    try {
      const s = window.bwVkStats?.();
      return s ? { lit: s.litPixels || 0, rendered: (s.v2 && s.v2.rendered) || s.rendered || 0 } : null;
    } catch (e) { return null; }
  }).catch(() => null);

  // CPU sampler: sums user+sys jiffies across the whole chromium process
  // tree via /proc. Distinguishes slow-but-working (CPU pegged) from wedged
  // (CPU idle + no log/lit/rendered movement).
  const { readFileSync, readdirSync } = await import('node:fs');
  const chromeJiffies = () => {
    let total = 0;
    try {
      for (const pid of readdirSync('/proc')) {
        if (!/^\d+$/.test(pid)) continue;
        try {
          const stat = readFileSync(`/proc/${pid}/stat`, 'utf8');
          const comm = (stat.match(/\(([^)]*)\)/) || [])[1] || '';
          if (!/chrom(e|ium)|headless|playwright|node/.test(comm)) continue;
          const f = stat.slice(stat.lastIndexOf(')') + 1).trim().split(/\s+/);
          total += (+f[11] || 0) + (+f[12] || 0); // utime + stime
        } catch {}
      }
    } catch {}
    return total;
  };

  let bestLit = 0, vkLane = false, pipeLane = false, focused = false, polls = 0, shotCount = 0;
  let lastRendered = 0, lastT = Date.now(), fpsSamples = [];
  const t0 = Date.now();
  const deadline = t0 + 35 * 60 * 1000; // cap: CPU-busy slow work may need it; watchdog aborts wedged runs far earlier
  // Fail-fast: staged gates + progress watchdog so a dead run aborts in
  // minutes instead of burning the full 25-min budget.
  let lastProgress = t0; // last time any progress signal moved
  let lastSig = '', lastJiffies;
  const sigOf = () => {
    const st = logs.length + ':' + bestLit + ':' + lastRendered;
    const m = logs.join('\n').match(/fszipurl: (\d+) MB fetched lazily so far/g);
    return st + ':' + (m ? m[m.length - 1] : 'nofetch');
  };
  let abortReason = '';
  while (Date.now() < deadline && !abortReason) {
    await page.waitForTimeout(30000);
    const elapsed = Date.now() - t0;
    const joined = logs.join('\n');
    // Gate 0 (immediate): hard page death — our own bug class, wasm link
    // failure, or missing resources. No point waiting.
    const hardFatal = joined.match(/Array buffer allocation failed|terminate called after throwing|Unhandled (division by zero|exception)|Assertion[^\n]*failed|pageerror: (Aborted\(|.*LinkError|.*is not valid JSON)|Failed to load resource[^\n]*404/);
    if (hardFatal) { abortReason = 'HARD-FATAL page dead on load: ' + hardFatal[0].slice(0, 120); break; }
    // Gate 1 (T+4min): page must be alive and producing output.
    if (elapsed > 4 * 60 * 1000 && logs.length <= 1) {
      abortReason = `NO-OUTPUT page produced ${logs.length} log line(s) in 4min (nav/serve broken?)`;
      break;
    }
    // Gate 2 (T+10min): Wine must have started booting.
    if (elapsed > 10 * 60 * 1000 && !/wine64 argv|FsZip:|XWire:|wined3d|vk32:|hl2\.exe/i.test(joined)) {
      abortReason = 'BOOT-STALL no Wine/gamezip activity 10min after nav';
      break;
    }
    if (!vkLane && /vk32: (trap|gipa HIT)/.test(joined)) {
      vkLane = true;
      console.log('>>> VULKAN LANE ACTIVE (vk32 traps in log)');
    }
    if (!pipeLane && /trap vkCreate(ShaderModule|GraphicsPipelines|RenderPass|Framebuffer)/.test(joined)) {
      pipeLane = true;
      console.log('>>> PIPELINE LANE ACTIVE (shaders/renderpasses seen)');
    }
    // Focus theory: Source idles pre-menu until the window gains focus, which
    // never happens headless. A real canvas click may deliver FocusIn via SDL
    // with no bridge changes. Click on window-show and every ~2min after.
    if (/Showing Window/.test(joined) && !focused) {
      focused = true;
      await page.bringToFront().catch(() => {});
      await page.mouse.click(640, 360).catch(() => {});
      console.log('>>> FOCUS-CLICK sent (window shown)');
    } else if (focused && (polls % 4 === 3)) {
      await page.mouse.click(640, 360).catch(() => {});
    }
    polls++;
    const st = await getStats();
    if (st) {
      if (st.lit > bestLit) {
        bestLit = st.lit;
        console.log('LIT', bestLit, 'RENDERED', st.rendered);
      }
      const now = Date.now();
      if (st.rendered > lastRendered && now > lastT) {
        const fps = (st.rendered - lastRendered) / ((now - lastT) / 1000);
        if (fps > 0 && fps < 500) { fpsSamples.push(+fps.toFixed(2)); }
        lastRendered = st.rendered;
        lastT = now;
      }
      if (st.lit > 500) {
        shotCount++;
        await page.screenshot({ path: `${dir}/portal-lit${st.lit}-${shotCount}.png` }).catch(() => {});
        console.log('SCREENSHOT lit=' + st.lit);
        if (bestLit > 20000 && fpsSamples.length >= 3 && process.env.HOLD_BROWSER !== '1') break;
      }
    }
    if (/pageerror:.*Exception|Aborted\(/.test(joined)) { console.log('FATAL ERROR DETECTED'); break; }
    // Progress watchdog: after the 10-min boot window, any movement in log
    // volume, lit pixels, rendered frames, or fetched MB counts as progress.
    // 6 min of total silence => stalled (crashed game, wedged fetch, hung GPU).
    // CPU sampler breaks the tie: pegged CPU + silence = slow work (keep
    // waiting, up to the cap); idle CPU + silence = wedged (abort).
    const sig = sigOf();
    const jiffies = chromeJiffies();
    const cpuDelta = jiffies - (lastJiffies ?? jiffies);
    lastJiffies = jiffies;
    // ~100 jiffies/30s across the tree ≈ idle browser noise; a pegged
    // emulated core is ~3000/30s. Threshold 1500 separates them.
    const cpuBusy = cpuDelta > 1500;
    if (sig !== lastSig) { lastSig = sig; lastProgress = Date.now(); }
    else if (elapsed > 10 * 60 * 1000 && Date.now() - lastProgress > 6 * 60 * 1000) {
      if (cpuBusy) {
        console.log(`STILL-WORKING no log growth 6min but CPU pegged (+${cpuDelta} jiffies) — extending`);
        lastProgress = Date.now(); // re-arm: as long as CPU stays pegged we wait for the cap
      } else {
        abortReason = `STALLED no progress for 6min (logs=${logs.length} lit=${bestLit} rendered=${lastRendered} cpuIdle)`;
        break;
      }
    }
    console.log(`[${Math.round((deadline - Date.now()) / 60000)}min left] logs=${logs.length} lit=${bestLit} vkLane=${vkLane} cpu${cpuBusy ? 'BUSY' : 'idle'}+${cpuDelta}`);
  }

  await page.screenshot({ path: `${dir}/portal-final.png` }).catch(() => {});
  logFile.end();
  const joined = logs.join('\n');
  const recentPresents = presents.filter(t => t >= (presents.at(-1) || 0) - 60000);
  const presentDuration = recentPresents.length > 1 ? (recentPresents.at(-1) - recentPresents[0]) / 1000 : 0;
  const summary = {
    tag, pass: bestLit > 500, litPixels: bestLit, vkLane,
    abortReason: abortReason || undefined,
    fpsSamples: fpsSamples.slice(-10),
    guestPresentFps: presentDuration ? (recentPresents.length - 1) / presentDuration : 0,
    guestPresentSampleSeconds: presentDuration,
    guestPresents: presents.length,
    meanFps: fpsSamples.length ? +(fpsSamples.reduce((a, b) => a + b, 0) / fpsSamples.length).toFixed(2) : 0,
    d3dOps: (joined.match(/d3dconf9: OP/g) || []).length,
    vkTraps: (joined.match(/vk32: trap/g) || []).length,
    logLines: logs.length,
  };
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary));
} catch (e) {
  console.log('PROBE ERROR', String(e.stack || e).slice(0, 400));
} finally {
  try { await browser?.close(); } catch {}
  try { await browserServer?.close(); } catch {}
  if (server) await new Promise(r => server.close(r));
  await slot.release();
  console.log('DONE');
}
