// Real-launcher demo evidence. This records observations; it makes no pass/fail
// judgment about whether a demo is playable.
import { chromium } from '@playwright/test';
import { appendFile, mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
process.chdir(repo);
const demos = ['notepad', 'explorer', 'snake', 'doom', 'celeste', 'chromium', 'portal', 'gangbeasts'];
const demo = process.env.DEMO;
if (!demos.includes(demo)) throw new Error(`DEMO is required and must be one of: ${demos.join(' ')}`);
// JIT=1 selects CPU64; JIT32=1 additionally selects CPU32. JIT=0 disables both.
const jit = process.env.JIT ?? '0';
if (jit !== '0' && jit !== '1') throw new Error('JIT must be 0 or 1');
const jit32 = process.env.JIT32;
if (jit32 !== undefined && jit32 !== '0' && jit32 !== '1') throw new Error('JIT32 must be 0 or 1');
const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/T/, 'T').slice(0, 15);
const defaultOut = join('test-results/demo-matrix', `${demo}-jit${jit}-${stamp}`);
const outDir = resolve(repo, process.env.OUT || defaultOut);
const longBoot = ['portal', 'celeste', 'gangbeasts', 'chromium'].includes(demo);
const mappedTimeout = Number(process.env.MAPPED_TIMEOUT_S || (longBoot ? 900 : 300));
if (!Number.isFinite(mappedTimeout) || mappedTimeout <= 0) throw new Error('MAPPED_TIMEOUT_S must be a positive number');
const threadDumpEnabled = process.env.BW_THREAD_DUMP === '1';
const threadDumpIntervalMs = Number(process.env.BW_THREAD_DUMP_INTERVAL_S || 60) * 1000;
if (!Number.isFinite(threadDumpIntervalMs) || threadDumpIntervalMs < 1000) throw new Error('BW_THREAD_DUMP_INTERVAL_S must be at least 1');
const capturePlayerLog = process.env.BW_CAPTURE_PLAYER_LOG === '1';
const wineDebug = process.env.BW_WINEDEBUG || '';
const mappedOnly = process.env.MAPPED_ONLY === '1';
if (mappedOnly && process.env.AUDIO_AUTOPLAY_ONLY !== '1') {
  throw new Error('MAPPED_ONLY requires AUDIO_AUTOPLAY_ONLY=1');
}
const bootOffsets = mappedOnly ? [] : (longBoot ? [5, 30, 60, 120, 240, 360] : [5, 30, 60, 120]);
const pixelMethod = 'in-page';

const metricsPath = join(outDir, 'metrics.jsonl');

let slot;
let server;
let browser;
let page;
let build;
let startedAt = 0;
let tClick = null;
let demoClickAtMs = null;
let tMapped = null;
let mappedAt = null;
let gameplayStartedAt = null;
let audioStartBeforePlayerClick = null;
let threadDumpTimer = null;
let threadDumpAttempts = 0;
let threadDumpInvocations = 0;
let threadDumpRuntimeUrl = '';
let sampling = false;
let samplerPromise = null;
let lastWasmMetricLine = null;
let lastMetricsSnapshot = null;
let lastJitStats = null;
let inputIndex = 0;
const consoleLines = [];
const pageErrors = [];
const httpErrors = [];
const screenshots = [];
let savedLogText = '';
let consoleErrorCount = 0;
let threadDumpLauncherPatched = false;

const elapsedSeconds = () => Number(((Date.now() - startedAt) / 1000).toFixed(3));

async function main() {
try {
  await mkdir(outDir, { recursive: true });
  await writeFile(metricsPath, '');
  await waitForBuildIdle();
  slot = await acquireProbeSlot();
  build = JSON.parse(await readFile(join(repo, 'dist/runtime/BUILD_INFO.json'), 'utf8'));
  if (!process.env.BW_TEST_BASE_URL) server = await serve(process.env.BW_TEST_DIST || 'dist');
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
    headless: true,
    args: [
      '--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan',
    ],
  });
  page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  const runtimeParams = new URLSearchParams(process.env.BW_TEST_URL_PARAMS || '');
  if (jit32 !== undefined) runtimeParams.set('jit32', jit32);
  const extraUrlParams = [...runtimeParams];
  // The launcher reads the frame URL, not a route.continue fetch rewrite.
  if (wineDebug) extraUrlParams.push(['winedbg', wineDebug]);
  if (extraUrlParams.length) {
    // Change the iframe's actual src before navigation. A route URL rewrite
    // changes the fetch URL but leaves window.location.search unchanged.
    await page.addInitScript(params => {
      const descriptor = Object.getOwnPropertyDescriptor(HTMLIFrameElement.prototype, 'src');
      if (!descriptor?.set || !descriptor?.get) throw new Error('Cannot instrument runtime iframe src');
      Object.defineProperty(HTMLIFrameElement.prototype, 'src', {
        configurable: true,
        enumerable: descriptor.enumerable,
        get() { return descriptor.get.call(this); },
        set(value) {
          const url = new URL(value, document.baseURI);
          if (url.pathname.endsWith('/runtime/index.html')) {
            for (const [key, paramValue] of params) url.searchParams.set(key, paramValue);
          }
          descriptor.set.call(this, url.toString());
        },
      });
    }, extraUrlParams);
  }
  await page.addInitScript(() => {
    globalThis.__bwAudioOutputMetrics ||= [];
    if (window === window.top) {
      window.addEventListener('message', event => {
        const runtimeFrame = document.querySelector('#display iframe');
        if (event.source !== runtimeFrame?.contentWindow || event.data?.bw64AudioMetric?.type !== 'bw-audio-metrics') return;
        globalThis.__bwAudioOutputMetrics.push({ ...event.data.bw64AudioMetric, observedAtMs: Date.now() });
      });
      return;
    }
    const NativeWorkletNode = globalThis.AudioWorkletNode;
    if (!NativeWorkletNode) return;
    globalThis.AudioWorkletNode = class extends NativeWorkletNode {
      constructor(context, name, options) {
        super(context, name, options);
        this.port.addEventListener('message', event => {
          if (event.data?.type === 'bw-audio-metrics')
            parent.postMessage({ bw64AudioMetric: event.data }, '*');
        });
        this.port.start();
      }
    };
  });
  if (threadDumpEnabled) {
    await page.addInitScript(() => {
      if (window.top !== window) return;
      const descriptor = Object.getOwnPropertyDescriptor(HTMLIFrameElement.prototype, 'src');
      if (!descriptor?.set || !descriptor?.get) throw new Error('Cannot instrument runtime iframe src');
      Object.defineProperty(HTMLIFrameElement.prototype, 'src', {
        configurable: true,
        enumerable: descriptor.enumerable,
        get() { return descriptor.get.call(this); },
        set(value) {
          const url = new URL(value, document.baseURI);
          if (url.pathname.endsWith('/runtime/index.html')) url.searchParams.set('bw64diag', 'threads');
          descriptor.set.call(this, url.toString());
        },
      });
    });
    await page.route('**/runtime/index.html*', route => {
      const url = new URL(route.request().url());
      url.searchParams.set('bw64diag', 'threads');
      if (wineDebug) url.searchParams.set('winedbg', wineDebug);
      return route.continue({ url: url.toString() });
    });
    await page.route('**/wine64-launcher.js*', async route => {
      const response = await route.fetch();
      const body = await response.text();
      const marker = 'if (param("unimpldump") === "1") ENV["BW64_UNIMPLDUMP"] = "1";';
      if (!body.includes(marker)) throw new Error('Could not install thread dump URL flag in Wine launcher');
      threadDumpRuntimeUrl = route.request().frame().url();
      if (new URL(threadDumpRuntimeUrl).searchParams.get('bw64diag') !== 'threads')
        throw new Error(`Thread dump URL flag missing from runtime frame: ${threadDumpRuntimeUrl}`);
      const diagnostics = [
        'if (param("bw64diag") === "threads") {',
        '  ENV["BW_THREAD_DUMP"] = "1";',
        '  ENV["BW64_CRASHRING"] = "1";',
        '  ENV["BW64_WSREAD"] = "1";',
        '  window.bw64CapturePlayerLogs = function() {',
        '    var FS = getFS(); if (!FS) return [];',
        '    var found = [];',
        '    function capture(path) { try { var stat = FS.stat(path); if (!FS.isFile(stat.mode)) return;',
        '      var content = FS.readFile(path, { encoding: "utf8" });',
        '      found.push({ path: path, bytes: stat.size, content: content.length > 524288 ? content.slice(-524288) : content });',
        '    } catch (e) {} }',
        '    function walk(path, depth) { if (depth < 0) return; var names; try { names = FS.readdir(path); } catch (e) { return; }',
        '      for (var i = 0; i < names.length; i++) { var name = names[i]; if (name === "." || name === "..") continue;',
        '        var child = path + "/" + name; var st; try { st = FS.stat(child); } catch (e) { continue; }',
        '        if (FS.isDir(st.mode)) walk(child, depth - 1);',
        '        else if (/^(output_log\\.txt|Player\\.log)$/i.test(name)) capture(child);',
        '      }',
        '    }',
        '    var apps = "/home/username/apps"; var appNames = []; try { appNames = FS.readdir(apps); } catch (e) {}',
        '    for (var i = 0; i < appNames.length; i++) if (/_Data$/i.test(appNames[i])) {',
        '      capture(apps + "/" + appNames[i] + "/output_log.txt"); capture(apps + "/" + appNames[i] + "/Player.log");',
        '    }',
        '    walk("/home/username/.wine/drive_c/users/username/AppData/LocalLow", 5);',
        '    walk("/home/username/.wine/drive_c/users/username/Local Settings/Application Data/Unity", 4);',
        '    return found;',
        '  };',
        '}',
        marker,
      ].join("\n                ");
      const instrumented = body.replace(marker, diagnostics);
      threadDumpLauncherPatched = true;
      return route.fulfill({ response, body: instrumented });
    });
  }
  startedAt = Date.now();
  page.on('console', message => {
    const text = message.text();
    consoleLines.push(`[${elapsedSeconds().toFixed(1)}s] ${message.type()}: ${text}`);
    if (message.type() === 'error') consoleErrorCount++;
    if (text.includes('bw64: METRICS ')) lastWasmMetricLine = text;
  });
  page.on('pageerror', error => {
    const line = `[${elapsedSeconds().toFixed(1)}s] pageerror: ${error.stack || error.message}`;
    pageErrors.push(line);
    consoleLines.push(line);
  });
  page.on('response', response => {
    if (response.status() < 400) return;
    let path = response.url();
    try { path = new URL(response.url()).pathname; } catch {}
    const line = `[${elapsedSeconds().toFixed(1)}s] http-error: ${response.status()} ${path}`;
    httpErrors.push(line);
    consoleLines.push(line);
  });

  const baseUrl = process.env.BW_TEST_BASE_URL || `http://127.0.0.1:${server.address().port}/`;
  await page.goto(baseUrl, { waitUntil: 'domcontentloaded' });
  await applyJitSetting(page, jit);
  try {
    await launchDemoGame(page, demo, { mappedTimeout, onClick: () => {
      tClick = elapsedSeconds();
      demoClickAtMs = Date.now();
      if (threadDumpEnabled) {
        threadDumpTimer = setInterval(async () => {
          threadDumpAttempts++;
          try {
            const frameHandle = await page.locator('#display iframe').elementHandle();
            const runtimeFrame = await frameHandle?.contentFrame();
            if (!runtimeFrame) throw new Error('runtime iframe is not available');
            const invoked = await runtimeFrame.evaluate(() => {
              if (typeof Module === 'undefined') throw new Error('runtime Module is not available');
              if (typeof Module._bw64_dump_guest_threads === 'function') {
                Module._bw64_dump_guest_threads();
                return 'export';
              }
              if (typeof Module.ccall === 'function') {
                Module.ccall('bw64_dump_guest_threads', null, [], []);
                return 'ccall';
              }
              throw new Error('thread dump export is not available');
            });
            threadDumpInvocations++;
            const line = `[${elapsedSeconds().toFixed(1)}s] BW_THREAD_DUMP_DRIVER invoked via ${invoked} (#${threadDumpInvocations})`;
            consoleLines.push(line);
            console.log(line);
          } catch (error) {
            const line = `[${elapsedSeconds().toFixed(1)}s] BW_THREAD_DUMP_DRIVER attempt failed: ${error.message || error}`;
            consoleLines.push(line);
            console.error(line);
          }
        }, threadDumpIntervalMs);
      }
    } });
  } catch (error) {
    if (error.name !== 'TimeoutError') throw error;
    const timeoutPath = join(outDir, 'timeout.png');
    await page.screenshot({ path: timeoutPath, fullPage: true }).catch(() => {});
    if (capturePlayerLog) {
      const guestLogs = await page.evaluate(() => {
        const frame = document.querySelector('#display iframe');
        const capture = frame?.contentWindow?.bw64CapturePlayerLogs;
        return typeof capture === 'function' ? capture() : { error: 'guest log capture hook unavailable' };
      }).catch(error => ({ error: error.message || String(error) }));
      await writeFile(join(outDir, 'player-logs.json'), JSON.stringify(guestLogs, null, 2) + '\n');
    }
    const pixels = await measurePlayerPixels(page).catch(() => ({ nonblankFraction: null, uniqueColors: null, pixelError: 'measurement failed' }));
    screenshots.push({ name: 'timeout.png', atSeconds: elapsedSeconds(), ...pixels });
    const audioAfterNoInput = await page.evaluate(() => {
      const frame = document.querySelector('#display iframe');
      const runtime = frame?.contentWindow;
      const sdlContext = runtime?.Module?.SDL2?.audioContext;
      const launchContext = runtime?.bwLaunchAudioContext || runtime?.parent?.bwLauncherAudioContext;
      return {
        checkedAtMs: Date.now(),
        crossOriginIsolated: runtime?.crossOriginIsolated === true,
        contextState: sdlContext?.state || launchContext?.state || null,
        sdlContextState: sdlContext?.state || null,
        launchContextState: launchContext?.state || null,
        sdlUsesLaunchContext: Boolean(sdlContext && launchContext && sdlContext === launchContext),
        iframeAllow: frame?.allow || '',
        worklet: runtime?.Module?.SDL2?.bwAudioWorklet === true,
        launcherGestureForwarded: runtime?.bwLauncherAudioGestureForwarded === true,
        frameActivation: runtime?.navigator?.userActivation ? {
          isActive: runtime.navigator.userActivation.isActive,
          hasBeenActive: runtime.navigator.userActivation.hasBeenActive,
        } : null,
        parentActivation: runtime?.parent?.navigator?.userActivation ? {
          isActive: runtime.parent.navigator.userActivation.isActive,
          hasBeenActive: runtime.parent.navigator.userActivation.hasBeenActive,
        } : null,
        audioOutputMetrics: globalThis.__bwAudioOutputMetrics || [],
      };
    }).catch(() => null);
    const audioOutputMetrics = audioAfterNoInput?.audioOutputMetrics || [];
    const nonzeroOutputFrames = audioOutputMetrics.reduce((total, metric) => total + Number(metric.nonzeroOutputFrames || 0), 0);
    const autoplayPassed = audioAfterNoInput?.contextState === 'running' && nonzeroOutputFrames > 0;
    const logText = await readLauncherLog(page);
    await writeFile(join(outDir, 'log-tail.txt'), logText.split('\n').slice(-400).join('\n') + '\n');
    await saveLogs(page);
    await writeFile(join(outDir, 'summary.json'), JSON.stringify({
      demo, jit: Number(jit), build, tClick, tMapped: null, clickToMappedSeconds: null,
      screenshots, pageErrorCount: pageErrors.length, consoleErrorCount: countConsoleErrors(),
      lastMetricsSnapshot, lastJitStats, stage: 'not-mapped', pixelMethod, httpErrorCount: httpErrors.length,
      audioAfterNoInput, audioMetricEvents: audioOutputMetrics.length, nonzeroOutputFrames, autoplayPassed,
      threadDumpEnabled, threadDumpLauncherPatched, threadDumpRuntimeUrl,
      threadDumpAttempts, threadDumpInvocations,
    }, null, 2) + '\n');
    console.log(JSON.stringify({ demo, jit: Number(jit), stage: 'not-mapped', audioAfterNoInput,
      audioMetricEvents: audioOutputMetrics.length, nonzeroOutputFrames, autoplayPassed, outDir: relative(repo, outDir) }));
    if (process.env.AUDIO_AUTOPLAY_REQUIRE === '1' && !autoplayPassed) process.exitCode = 1;
    return;
  }

  tMapped = elapsedSeconds();
  mappedAt = Date.now();
  startMetricsSampler();
  for (const seconds of bootOffsets) {
    const due = mappedAt + seconds * 1000;
    if (due > Date.now()) await page.waitForTimeout(due - Date.now());
    await takeScreenshot(`boot-${seconds}s.png`);
  }
  audioStartBeforePlayerClick = await page.evaluate(() => {
    const frame = document.querySelector('#display iframe');
    const runtime = frame?.contentWindow;
    const context = runtime?.Module?.SDL2?.audioContext;
    const launchContext = runtime?.bwLaunchAudioContext || runtime?.parent?.bwLauncherAudioContext;
    return {
      checkedAtMs: Date.now(),
      crossOriginIsolated: runtime?.crossOriginIsolated === true,
      contextState: context?.state || launchContext?.state || null,
      sdlContextState: context?.state || null,
      launchContextState: launchContext?.state || null,
      sdlUsesLaunchContext: Boolean(context && launchContext && context === launchContext),
      iframeAllow: frame?.allow || '',
      worklet: runtime?.Module?.SDL2?.bwAudioWorklet === true,
      launcherGestureForwarded: runtime?.bwLauncherAudioGestureForwarded === true,
      frameActivation: runtime?.navigator?.userActivation ? {
        isActive: runtime.navigator.userActivation.isActive,
        hasBeenActive: runtime.navigator.userActivation.hasBeenActive,
      } : null,
      parentActivation: runtime?.parent?.navigator?.userActivation ? {
        isActive: runtime.parent.navigator.userActivation.isActive,
        hasBeenActive: runtime.parent.navigator.userActivation.hasBeenActive,
      } : null,
    };
  });
  if (process.env.AUDIO_AUTOPLAY_ONLY === '1') {
    const observeMs = Number(process.env.AUDIO_AUTOPLAY_OBSERVE_MS || 30000);
    await page.waitForTimeout(observeMs);
    sampling = false;
    await samplerPromise;
    await sampleMetrics();
    await saveLogs(page);
    const audioAfterNoInput = await page.evaluate(() => {
      const frame = document.querySelector('#display iframe');
      const runtime = frame?.contentWindow;
      const context = runtime?.Module?.SDL2?.audioContext;
      const launchContext = runtime?.bwLaunchAudioContext || runtime?.parent?.bwLauncherAudioContext;
      return {
        checkedAtMs: Date.now(),
        crossOriginIsolated: runtime?.crossOriginIsolated === true,
        contextState: context?.state || launchContext?.state || null,
        sdlContextState: context?.state || null,
        launchContextState: launchContext?.state || null,
        sdlUsesLaunchContext: Boolean(context && launchContext && context === launchContext),
        iframeAllow: frame?.allow || '',
        worklet: runtime?.Module?.SDL2?.bwAudioWorklet === true,
        launcherGestureForwarded: runtime?.bwLauncherAudioGestureForwarded === true,
        frameActivation: runtime?.navigator?.userActivation ? {
          isActive: runtime.navigator.userActivation.isActive,
          hasBeenActive: runtime.navigator.userActivation.hasBeenActive,
        } : null,
        parentActivation: runtime?.parent?.navigator?.userActivation ? {
          isActive: runtime.parent.navigator.userActivation.isActive,
          hasBeenActive: runtime.parent.navigator.userActivation.hasBeenActive,
        } : null,
      };
    });
    const metricRows = (await readFile(metricsPath, 'utf8')).split('\n').filter(Boolean).map(line => JSON.parse(line));
    const audioOutputMetrics = metricRows.flatMap(row => row.audioOutputMetrics || [])
      .filter(metric => metric.observedAtMs >= demoClickAtMs);
    const nonzeroOutputFrames = audioOutputMetrics.reduce((total, metric) => total + Number(metric.nonzeroOutputFrames || 0), 0);
    const audioTimeline = metricRows.map(row => ({
      atSeconds: row.atSeconds,
      contextState: row.audioRuntime?.contextState || null,
      sdlContextState: row.audioRuntime?.sdlContextState || null,
      launchContextState: row.audioRuntime?.launchContextState || null,
      sdlUsesLaunchContext: row.audioRuntime?.sdlUsesLaunchContext === true,
      worklet: row.audioRuntime?.worklet === true,
      launcherGestureForwarded: row.audioRuntime?.launcherGestureForwarded === true,
    }));
    const autoplayPassed = audioAfterNoInput.contextState === 'running' && nonzeroOutputFrames > 0;
    const summary = {
      demo, jit: Number(jit), build, tClick, tMapped,
      clickToMappedSeconds: Number((tMapped - tClick).toFixed(3)),
      screenshots, pageErrorCount: pageErrors.length, consoleErrorCount: countConsoleErrors(),
      lastMetricsSnapshot, lastJitStats, stage: 'mapped-no-input', pixelMethod, httpErrorCount: httpErrors.length,
      demoClickAtMs, audioStartBeforePlayerClick, audioAfterNoInput, observeMs,
      audioTimeline, audioMetricEvents: audioOutputMetrics.length, nonzeroOutputFrames, autoplayPassed,
      threadDumpEnabled, threadDumpLauncherPatched, threadDumpRuntimeUrl,
      threadDumpAttempts, threadDumpInvocations,
    };
    await writeFile(join(outDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
    console.log(JSON.stringify({ demo, jit: Number(jit), stage: 'mapped-no-input',
      audioStartBeforePlayerClick, audioAfterNoInput, audioMetricEvents: audioOutputMetrics.length,
      nonzeroOutputFrames, autoplayPassed, observeMs, outDir: relative(repo, outDir) }));
    if (process.env.AUDIO_AUTOPLAY_REQUIRE === '1' && !autoplayPassed) process.exitCode = 1;
    return;
  }
  await page.mouse.click(...await displayCenter());
  await runInputScript();
  sampling = false;
  await samplerPromise;
  await sampleMetrics();
  await saveLogs(page);
  // "mapped" only proves Wine opened a window. A user also needs the launcher's
  // first-frame overlay to clear, otherwise the program looks stuck forever.
  const overlayCleared = await page.evaluate(() => {
    const loading = document.getElementById('loading');
    const progress = document.getElementById('launchProgress');
    return Boolean(loading?.hidden) && (!progress || progress.hidden);
  }).catch(() => false);
  if (!overlayCleared) { process.exitCode = 1; console.error('FAIL: launcher still shows the first-frame overlay after the program mapped'); }
  const summary = {
    overlayCleared,
    demo, jit: Number(jit), build, tClick, tMapped,
    clickToMappedSeconds: Number((tMapped - tClick).toFixed(3)),
    screenshots, pageErrorCount: pageErrors.length, consoleErrorCount: countConsoleErrors(),
    lastMetricsSnapshot, lastJitStats, stage: 'mapped', pixelMethod, httpErrorCount: httpErrors.length,
    gameplayStartedAtMs: gameplayStartedAt,
    demoClickAtMs,
    audioStartBeforePlayerClick,
    threadDumpEnabled, threadDumpLauncherPatched, threadDumpRuntimeUrl,
    threadDumpAttempts, threadDumpInvocations,
  };
  await writeFile(join(outDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
  console.log(JSON.stringify({ demo, jit: Number(jit), stage: 'mapped', overlayCleared, clickToMappedSeconds: summary.clickToMappedSeconds,
    finalScreenshot: screenshots.at(-1), pageErrors: summary.pageErrorCount, consoleErrors: summary.consoleErrorCount,
    outDir: relative(repo, outDir) }));
} finally {
  clearInterval(threadDumpTimer);
  sampling = false;
  await samplerPromise?.catch(() => {});
  await browser?.close().catch(() => {});
  if (startedAt) {
    await writeFile(join(outDir, 'console.txt'), consoleLines.join('\n') + (consoleLines.length ? '\n' : '')).catch(() => {});
    await writeFile(join(outDir, 'log.txt'), savedLogText).catch(() => {});
  }
  if (server) await new Promise(resolveClose => server.close(resolveClose)).catch(() => {});
  await slot?.release();
}
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await main();

function countConsoleErrors() {
  return consoleErrorCount;
}

async function readLauncherLog(currentPage) {
  return await currentPage.locator('#log').textContent().catch(() => '') || '';
}

async function saveLogs(currentPage) {
  const combined = [...consoleLines, ...pageErrors.filter(line => !consoleLines.includes(line)), ...httpErrors.filter(line => !consoleLines.includes(line))];
  await writeFile(join(outDir, 'console.txt'), combined.join('\n') + (combined.length ? '\n' : ''));
  savedLogText = await readLauncherLog(currentPage);
  await writeFile(join(outDir, 'log.txt'), savedLogText);
}

async function takeScreenshot(name) {
  const path = join(outDir, name);
  await page.screenshot({ path, fullPage: true });
  const pixels = await measurePlayerPixels(page);
  const record = { name, atSeconds: elapsedSeconds(), ...pixels };
  screenshots.push(record);
  return record;
}

async function measurePlayerPixels(currentPage) {
  return currentPage.evaluate(() => {
    const display = document.querySelector('#display');
    const frame = display?.querySelector('iframe');
    const canvas = document.querySelector('#vkCanvas') || document.querySelector('#gpuCanvas')
      || frame?.contentDocument?.querySelector('#canvas');
    const rect = display?.getBoundingClientRect();
    const region = rect ? { x: rect.x, y: rect.y, width: rect.width, height: rect.height } : null;
    if (!canvas || !canvas.width || !canvas.height) {
      return { nonblankFraction: null, uniqueColors: null, pixelRegion: region, pixelSource: null };
    }
    try {
      const copy = document.createElement('canvas');
      copy.width = canvas.width;
      copy.height = canvas.height;
      const context = copy.getContext('2d', { willReadFrequently: true });
      context.drawImage(canvas, 0, 0, copy.width, copy.height);
      const rgba = context.getImageData(0, 0, copy.width, copy.height).data;
      let nonblank = 0;
      const colors = new Set();
      for (let at = 0; at < rgba.length; at += 4) {
        const r = rgba[at], g = rgba[at + 1], b = rgba[at + 2];
        if (Math.max(r, g, b) > 16) nonblank++;
        if (colors.size < 10000) colors.add((r << 16) | (g << 8) | b);
      }
      return {
        nonblankFraction: nonblank / (copy.width * copy.height),
        uniqueColors: Math.min(10000, colors.size),
        pixelRegion: region,
        pixelSource: canvas.id ? `#${canvas.id}` : 'canvas',
        pixelWidth: copy.width,
        pixelHeight: copy.height,
      };
    } catch (error) {
      return { nonblankFraction: null, uniqueColors: null, pixelRegion: region,
        pixelSource: canvas.id ? `#${canvas.id}` : 'canvas', pixelError: error.message };
    }
  });
}

function startMetricsSampler() {
  sampling = true;
  samplerPromise = (async () => {
    let due = mappedAt + 5000;
    while (sampling) {
      const wait = due - Date.now();
      if (wait > 0) await new Promise(resolveWait => setTimeout(resolveWait, wait));
      if (!sampling) break;
      await sampleMetrics();
      due += 5000;
      if (due < Date.now()) due = Date.now() + 5000;
    }
  })();
}

async function sampleMetrics() {
  const pageTier = await page.evaluate(() => {
    try { return window.bwVkTier?.metrics?.() || null; }
    catch (error) { return { error: error.message || String(error) }; }
  }).catch(() => null);
  let jitStats = null;
  let jitStatsError = null;
  let audioRuntime = null;
  try {
    const frameHandle = await page.locator('#display iframe').elementHandle();
    const runtimeFrame = await frameHandle?.contentFrame();
    if (!runtimeFrame) throw new Error('runtime iframe is not available');
    audioRuntime = await runtimeFrame.evaluate(() => ({
      crossOriginIsolated: globalThis.crossOriginIsolated,
      worklet: globalThis.Module?.SDL2?.bwAudioWorklet === true,
      launcherGestureForwarded: globalThis.bwLauncherAudioGestureForwarded === true,
      contextState: globalThis.Module?.SDL2?.audioContext?.state || globalThis.bwLaunchAudioContext?.state || null,
      sdlContextState: globalThis.Module?.SDL2?.audioContext?.state || null,
      launchContextState: globalThis.bwLaunchAudioContext?.state || null,
      sdlUsesLaunchContext: Boolean(globalThis.Module?.SDL2?.audioContext &&
        globalThis.Module.SDL2.audioContext === globalThis.bwLaunchAudioContext),
      frameActivation: navigator.userActivation ? {
        isActive: navigator.userActivation.isActive,
        hasBeenActive: navigator.userActivation.hasBeenActive,
      } : null,
      parentActivation: (() => {
        try {
          const activation = parent.navigator.userActivation;
          return activation ? { isActive: activation.isActive, hasBeenActive: activation.hasBeenActive } : null;
        } catch { return null; }
      })(),
    }));
    jitStats = await runtimeFrame.evaluate(() => {
      if (typeof Module === 'undefined' || typeof Module.ccall !== 'function')
        throw new Error('runtime Module export is not available');
      const result = Module.ccall('bw64_jit_stats_json_text', 'string', [], []);
      if (typeof result === 'string') return JSON.parse(result);
      if (result && typeof Module.UTF8ToString === 'function') return JSON.parse(Module.UTF8ToString(result));
      throw new Error('bw64_jit_stats_json_text returned no readable text');
    });
  } catch (error) { jitStatsError = error.message || String(error); }
  const wasmLine = lastWasmMetricLine;
  const wasmSnapshot = parseWasmMetricLine(wasmLine);
  const record = {
    atSeconds: elapsedSeconds(),
    gameplayStartedAtMs: gameplayStartedAt,
    audioOutputMetrics: await page.evaluate(() => globalThis.__bwAudioOutputMetrics?.splice(0) || []),
    audioRuntime,
    frameMetricsSource: {
      wasm: 'runtime iframe console message emitted by Module.print; launcher app feeds bw64: METRICS lines',
      pageTier: 'launcher page window.bwVkTier.metrics()',
    },
    frameMetricsSnapshot: { wasmConsoleLine: wasmLine, wasm: wasmSnapshot, pageTier },
    jitStats,
    jitStatsError,
  };
  lastMetricsSnapshot = record.frameMetricsSnapshot;
  lastJitStats = record.jitStats;
  await appendFile(metricsPath, JSON.stringify(record) + '\n');
}

function parseWasmMetricLine(line) {
  if (!line) return null;
  const marker = 'bw64: METRICS ';
  const at = line.indexOf(marker);
  if (at < 0) return null;
  const json = line.slice(at + marker.length).split(/\r?\n/, 1)[0].trim();
  try { return JSON.parse(json); } catch { return null; }
}

async function displayCenter() {
  const box = await page.locator('#display').boundingBox();
  if (!box) throw new Error('#display has no bounding box');
  return [box.x + box.width / 2, box.y + box.height / 2];
}

async function inputScreenshot(label) {
  inputIndex++;
  return takeScreenshot(`input-${inputIndex}-${label}.png`);
}

async function inputAction(label, action, settleMs = 1500) {
  await action();
  await page.waitForTimeout(settleMs);
  await inputScreenshot(label);
}

async function holdKey(key, durationMs) {
  await page.keyboard.down(key);
  try { await page.waitForTimeout(durationMs); }
  finally { await page.keyboard.up(key); }
}

export async function playInputScript(page, demo, { screenshot = null, center = null } = {}) {
  const inputScreenshot = async label => screenshot ? screenshot(label) : null;
  const inputAction = async (label, action, settleMs = 1500) => {
    await action();
    await page.waitForTimeout(settleMs);
    await inputScreenshot(label);
  };
  const displayCenter = center || (async () => {
    const box = await page.locator('#display').boundingBox();
    if (!box) throw new Error('#display has no bounding box');
    return [box.x + box.width / 2, box.y + box.height / 2];
  });
  const holdKey = async (key, durationMs) => {
    await page.keyboard.down(key);
    try { await page.waitForTimeout(durationMs); }
    finally { await page.keyboard.up(key); }
  };
  if (demo === 'notepad') {
    await inputAction('type-first-line', () => page.keyboard.type('winbox works 123', { delay: 80 }));
    await inputAction('enter', () => page.keyboard.press('Enter'));
    await inputAction('type-second-line', () => page.keyboard.type('second line', { delay: 80 }));
  } else if (demo === 'explorer') {
    await inputAction('tab', () => page.keyboard.press('Tab'));
    await inputAction('down-three', async () => { for (let i = 0; i < 3; i++) await page.keyboard.press('ArrowDown'); });
    await inputAction('enter', () => page.keyboard.press('Enter'));
    await page.waitForTimeout(3500);
  } else if (demo === 'snake') {
    await inputAction('up', () => page.keyboard.press('ArrowUp'));
    await inputAction('left', () => page.keyboard.press('ArrowLeft'));
    await inputAction('down', () => page.keyboard.press('ArrowDown'));
    for (let i = 1; i <= 5; i++) {
      await page.waitForTimeout(1000);
      await inputScreenshot(`state-${i}`);
    }
  } else if (demo === 'doom') {
    for (let i = 1; i <= 4; i++) await inputAction(`enter-${i}`, () => page.keyboard.press('Enter'));
    await page.waitForTimeout(5000);
    gameplayStartedAt = Date.now();
    await inputAction('forward', () => holdKey('ArrowUp', 2000));
    await inputAction('control', () => page.keyboard.press('Control'));
    await inputAction('turn-left', () => holdKey('ArrowLeft', 1000));
  } else if (demo === 'celeste') {
    for (let i = 1; i <= 3; i++) {
      await inputAction(`c-${i}`, () => page.keyboard.press('c'));
      if (i < 3) await page.waitForTimeout(1500);
    }
    await page.waitForTimeout(15000);
    await inputAction('right', () => holdKey('ArrowRight', 1000));
    await inputAction('c-again', () => page.keyboard.press('c'));
    await inputAction('left', () => holdKey('ArrowLeft', 1000));
    await inputAction('x', () => page.keyboard.press('x'));
  } else if (demo === 'chromium') {
    await page.mouse.click(...await displayCenter());
    await inputAction('type-search', () => page.keyboard.type('boxedwine works', { delay: 80 }));
    await inputAction('enter', () => page.keyboard.press('Enter'));
  } else if (demo === 'portal') {
    await inputAction('escape', () => page.keyboard.press('Escape'));
    await page.waitForTimeout(3000);
    const [x, y] = await displayCenter();
    await inputAction('click-center', () => page.mouse.click(x, y));
    await inputAction('forward', () => holdKey('w', 2000));
    await inputAction('mouse-right', () => page.mouse.move(x + 200, y, { steps: 10 }));
  } else if (demo === 'gangbeasts') {
    await inputAction('enter-1', () => page.keyboard.press('Enter'));
    await page.waitForTimeout(3500);
    await inputAction('enter-2', () => page.keyboard.press('Enter'));
    await page.waitForTimeout(8500);
    await inputAction('right', () => holdKey('ArrowRight', 1000));
    await inputAction('space', () => page.keyboard.press('Space'));
  }
}

async function runInputScript() {
  await playInputScript(page, demo, { screenshot: inputScreenshot, center: displayCenter });
}

export async function applyJitSetting(page, jit) {
  await page.waitForFunction(() => window.wineGames, null, { timeout: 60000 });
  await page.click('#settingsButton');
  await page.locator('#jit').selectOption(jit === '1' ? 'on' : 'off');
  await page.keyboard.press('Escape');
  await page.reload();
  await page.waitForFunction(() => window.wineGames, null, { timeout: 60000 });
  const actualJit = await page.locator('#jit').inputValue();
  if (actualJit !== (jit === '1' ? 'on' : 'off')) throw new Error(`Settings reload selected JIT=${actualJit}, expected ${jit}`);
}

export async function launchDemoGame(page, demo, { mappedTimeout, onClick = null } = {}) {
  await page.locator(`[data-game="${demo}"]`).click();
  if (onClick) onClick();
  await page.waitForFunction(() => document.getElementById('log')?.textContent.includes('first window mapped'), null,
    { timeout: mappedTimeout * 1000 });
}
