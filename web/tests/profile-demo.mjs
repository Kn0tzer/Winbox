// Host CPU profiling harness for a real demo run.
//
// Launches the same launcher/demo path as demo-matrix.mjs, attaches a raw CDP
// session to the browser with flatten:true auto-attach (recursively, so every
// page/iframe/worker/pthread worker is covered), samples V8 CPU profiles at
// 1 ms, keeps the demo's input script playing during the window, and records
// the guest-side per-mapped-file split from BW_GUEST_PROFILE.
//
// Env:
//   DEMO              notepad|explorer|snake|doom|celeste|chromium|portal|gangbeasts
//   JIT               0|1 (default 0, as in demo-matrix)
//   PROFILE_START     mapped+<s> | firstframe+<s>   (default firstframe+20)
//   PROFILE_S         window seconds                (default 60)
//   MAPPED_TIMEOUT_S  seconds to wait for the first window (demo-matrix default)
//   OUT               output directory
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
process.chdir(repo);

const demos = ['notepad', 'explorer', 'snake', 'doom', 'celeste', 'chromium', 'portal', 'gangbeasts'];
const demo = process.env.DEMO;
if (!demos.includes(demo)) throw new Error(`DEMO is required and must be one of: ${demos.join(' ')}`);
const jit = process.env.JIT ?? '0';
if (jit !== '0' && jit !== '1') throw new Error('JIT must be 0 or 1');
const profileSeconds = Number(process.env.PROFILE_S || 60);
if (!Number.isFinite(profileSeconds) || profileSeconds <= 0) throw new Error('PROFILE_S must be positive');
const startSpec = process.env.PROFILE_START || 'firstframe+20';
const startMatch = /^(mapped|firstframe)\+([0-9]+(?:\.[0-9]+)?)$/.exec(startSpec);
if (!startMatch) throw new Error('PROFILE_START must be mapped+<s> or firstframe+<s>');
const startAnchor = startMatch[1];
const startOffsetS = Number(startMatch[2]);
const longBoot = ['portal', 'celeste', 'gangbeasts', 'chromium'].includes(demo);
const mappedTimeout = Number(process.env.MAPPED_TIMEOUT_S || (longBoot ? 900 : 300));
if (!Number.isFinite(mappedTimeout) || mappedTimeout <= 0) throw new Error('MAPPED_TIMEOUT_S must be a positive number');
const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/T/, 'T').slice(0, 15);
const outDir = resolve(repo, process.env.OUT || join('test-results/profile', `${demo}-jit${jit}-${stamp}`));

const startWallMs = Date.now();
const elapsedSeconds = () => Number(((Date.now() - startWallMs) / 1000).toFixed(3));
const sleep = ms => new Promise(resolveSleep => setTimeout(resolveSleep, ms));

function readLoadAverage() {
  try { return Number(readFileSync('/proc/loadavg', 'utf8').split(' ')[0]); }
  catch { return null; }
}

async function waitForLoadAverageBelowThreshold(summary) {
  const waitStartedAt = Date.now();
  let current = readLoadAverage();
  while (current !== null && current >= 2.5 && Date.now() - waitStartedAt < 45 * 60 * 1000) {
    console.log(`profile-demo: load average ${current}; waiting 30 seconds before profiling`);
    await sleep(30_000);
    current = readLoadAverage();
  }
  summary.loadAverageWaitSeconds = Number(((Date.now() - waitStartedAt) / 1000).toFixed(1));
  summary.loadAverageReady = current;
  summary.loadAverageWaitExpired = current !== null && current >= 2.5;
}

const probeSlot = await acquireProbeSlot();
try {
  await mkdir(outDir, { recursive: true });
  await main();
} finally {
  await probeSlot.release();
}

async function main() {
await waitForBuildIdle();
const build = JSON.parse(await readFile(join(repo, 'dist/runtime/BUILD_INFO.json'), 'utf8'));

const server = await serve('dist');
const baseUrl = process.env.BW_TEST_BASE_URL || `http://127.0.0.1:${server.address().port}/`;

const userDataDir = await mkdtemp('/tmp/winbox-profile-');
const chrome = spawn(process.env.CHROME_PATH || '/usr/local/bin/chromium', [
  '--headless=new', '--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
  '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
  '--enable-features=Vulkan', '--remote-debugging-port=0', `--user-data-dir=${userDataDir}`, 'about:blank',
], { stdio: 'ignore' });

let browser;
let ws;
let page;
const sessions = new Map();       // sessionId -> { targetInfo, profileFile, profilerStarted }
const targets = [];               // { sessionId, targetId, type, url, title, profileFile }
let windowActive = false;
let nextId = 0;
const pending = new Map();

const loadStart = readLoadAverage();
const summary = {
  demo, jit: Number(jit), build, profileStart: startSpec, profileSeconds,
  loadAverageRunStart: loadStart, loadAverageWindowStart: null, loadAverageEnd: null,
  startedAt: new Date().toISOString(), tClick: null, tMapped: null, tFirstFrame: null,
  windowStartSeconds: null, windowEndSeconds: null, fallback: false, stage: 'running',
};

try {
  const devtoolsPort = await waitForDevTools(userDataDir, chrome);
  ws = new WebSocket(`ws://127.0.0.1:${devtoolsPort.port}${devtoolsPort.path}`);
  await new Promise((resolveOpen, rejectOpen) => { ws.on('open', resolveOpen); ws.on('error', rejectOpen); });
  ws.on('message', message => { handleCdp(JSON.parse(message.toString())).catch(error => console.error('cdp:', error.message)); });
  await cdp('Target.setDiscoverTargets', { discover: true });
  await cdp('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true });

  browser = await chromium.connectOverCDP(`http://127.0.0.1:${devtoolsPort.port}`);
  const context = browser.contexts()[0] || await browser.newContext();
  page = await context.newPage();
  await page.setViewportSize({ width: 1440, height: 1000 });

  const { applyJitSetting, launchDemoGame, playInputScript } = await import('./demo-matrix.mjs');

  // guestprof=1 is read by wine64-launcher from the parent page's query string,
  // which enables BW_GUEST_PROFILE in the runtime.
  const launchUrl = baseUrl + (baseUrl.includes('?') ? '&' : '?') + 'guestprof=1';
  await page.goto(launchUrl, { waitUntil: 'domcontentloaded' });
  await applyJitSetting(page, jit);
  try {
    await launchDemoGame(page, demo, { mappedTimeout, onClick: () => { summary.tClick = elapsedSeconds(); } });
  } catch (error) {
    if (error.name !== 'TimeoutError') throw error;
    summary.stage = 'not-mapped';
    await page.screenshot({ path: join(outDir, 'timeout.png'), fullPage: true }).catch(() => {});
    await writeFile(join(outDir, 'log-tail.txt'),
      ((await page.locator('#log').textContent().catch(() => '')) || '').split('\n').slice(-400).join('\n') + '\n');
    return;
  }
  summary.tMapped = elapsedSeconds();

  // Match demo-matrix's focus click so keyboard input reaches the guest window.
  const displayBox = await page.locator('#display').boundingBox();
  if (displayBox) await page.mouse.click(displayBox.x + displayBox.width / 2, displayBox.y + displayBox.height / 2);

  // firstframe: first non-blank frame on the runtime canvas. Used only when
  // PROFILE_START anchored at firstframe.
  let firstFrameDeadline = null;
  if (startAnchor === 'firstframe') firstFrameDeadline = Date.now() + mappedTimeout * 1000;
  const frameStart = Date.now();
  while (startAnchor === 'firstframe' && summary.tFirstFrame === null) {
    if (await hasVisibleFrame(page)) {
      summary.tFirstFrame = elapsedSeconds();
      break;
    }
    const now = Date.now();
    const fallbackStart = firstFrameDeadline - profileSeconds * 1000;
    if (now >= firstFrameDeadline) {
      // No frame before the mapped timeout: profile the PROFILE_S seconds
      // before the deadline instead (as required for portal).
      summary.fallback = true;
      console.error('profile-demo: no first frame before timeout; profiling the last',
        profileSeconds, 's before the timeout');
      break;
    }
    if (summary.fallback) break;
    if (now >= fallbackStart) {
      // Arm the fallback so startAt lands exactly profileSeconds before the deadline.
      summary.fallback = true;
      console.error('profile-demo: no first frame yet; arming fallback window before timeout');
      break;
    }
    await sleep(Math.min(250, Math.max(50, frameStart + mappedTimeout * 1000 - now)));
  }

  let startAtMs;
  if (summary.fallback) {
    startAtMs = firstFrameDeadline - profileSeconds * 1000;
  } else if (startAnchor === 'mapped') {
    startAtMs = startWallMs + summary.tMapped * 1000 + startOffsetS * 1000;
  } else {
    startAtMs = startWallMs + (summary.tFirstFrame ?? summary.tMapped) * 1000 + startOffsetS * 1000;
  }
  if (startAtMs > Date.now()) await sleep(startAtMs - Date.now());

  await waitForLoadAverageBelowThreshold(summary);

  summary.windowStartSeconds = elapsedSeconds();
  summary.loadAverageWindowStart = readLoadAverage();
  await page.screenshot({ path: join(outDir, 'window-start.png'), fullPage: true }).catch(() => {});
  const guestReset = await callGuest(page, 'bw64_guest_profile_reset');
  if (guestReset.error) summary.guestProfileError = guestReset.error;

  windowActive = true;
  for (const sessionId of sessions.keys()) await startProfiler(sessionId);

  // Keep playing the demo's input script for the whole window so the profile is
  // gameplay, not a menu/idle screen.
  let stopInput = false;
  const inputLoop = (async () => {
    while (!stopInput) {
      try { await playInputScript(page, demo); } catch (error) { if (stopInput) break; console.error('input:', error.message); await sleep(1000); }
    }
  })();

  await sleep(profileSeconds * 1000);
  stopInput = true;
  summary.windowEndSeconds = elapsedSeconds();
  summary.loadAverageEnd = readLoadAverage();
  windowActive = false;

  for (const sessionId of [...sessions.keys()]) await stopProfiler(sessionId);
  const guestJson = await callGuest(page, 'bw64_guest_profile_json');
  if (guestJson.error) summary.guestProfileError = guestJson.error;
  else await writeFile(join(outDir, 'guest-profile.json'), guestJson.value + '\n');
  summary.guestProfileBytes = guestJson.value ? guestJson.value.length : 0;

  await Promise.race([inputLoop, sleep(3000)]);
  await page.screenshot({ path: join(outDir, 'window-end.png'), fullPage: true }).catch(() => {});

  await writeFile(join(outDir, 'targets.json'), JSON.stringify(targets, null, 2) + '\n');
  summary.stage = 'profiled';
} finally {
  windowActive = false;
  summary.finishedAt = new Date().toISOString();
  summary.loadAverageEnd = summary.loadAverageEnd ?? readLoadAverage();
  await writeFile(join(outDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n').catch(() => {});
  console.log(JSON.stringify({ demo, jit: Number(jit), stage: summary.stage, outDir: relative(repo, outDir),
    windowStartSeconds: summary.windowStartSeconds, windowEndSeconds: summary.windowEndSeconds }));
  try { ws?.close(); } catch {}
  await browser?.close().catch(() => {});
  chrome.kill('SIGKILL');
  await new Promise(resolveExit => { chrome.once('exit', resolveExit); setTimeout(resolveExit, 2000); });
  await rm(userDataDir, { recursive: true, force: true }).catch(() => {});
  await new Promise(resolveClose => server.close(resolveClose)).catch(() => {});
}
}

async function waitForDevTools(directory, child) {
  for (let i = 0; i < 150; i++) {
    try {
      const [port, path] = (await readFile(join(directory, 'DevToolsActivePort'), 'utf8')).trim().split('\n');
      if (port && path) return { port: Number(port), path };
    } catch {}
    if (child.exitCode !== null) throw new Error(`chromium exited early (code ${child.exitCode})`);
    await sleep(200);
  }
  throw new Error('chromium did not expose a DevTools endpoint');
}

function cdp(method, params = {}, sessionId = undefined) {
  const id = ++nextId;
  return new Promise((resolveSend, rejectSend) => {
    pending.set(id, { resolve: resolveSend, reject: rejectSend, method });
    ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
  });
}

async function handleCdp(message) {
  if (message.id && pending.has(message.id)) {
    const item = pending.get(message.id);
    pending.delete(message.id);
    if (message.error) item.reject(new Error(`${item.method}: ${message.error.message}`));
    else item.resolve(message.result);
    return;
  }
  if (message.method === 'Target.attachedToTarget') {
    const { sessionId, targetInfo } = message.params;
    if (sessions.has(sessionId)) return;
    const record = { sessionId, profileFile: null, profilerStarted: false };
    sessions.set(sessionId, record);
    const entry = { sessionId, targetId: targetInfo.targetId, type: targetInfo.type,
      url: targetInfo.url, title: targetInfo.title, profileFile: null };
    targets.push(entry);
    try {
      await cdp('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }, sessionId);
    } catch {}
    await enableProfiler(sessionId);
    if (windowActive) await startProfiler(sessionId);
    return;
  }
  if (message.method === 'Target.detachedFromTarget') {
    const sessionId = message.params.sessionId;
    await stopProfiler(sessionId);
    sessions.delete(sessionId);
  }
}

async function enableProfiler(sessionId) {
  try {
    await cdp('Profiler.enable', {}, sessionId);
    await cdp('Profiler.setSamplingInterval', { interval: 1000 }, sessionId);
  } catch {
    // Non-JS targets (rare) simply have no profiler domain.
  }
}

async function startProfiler(sessionId) {
  const session = sessions.get(sessionId);
  if (!session || session.profilerStarted) return;
  try {
    await cdp('Profiler.start', {}, sessionId);
    session.profilerStarted = true;
  } catch {}
}

async function stopProfiler(sessionId) {
  const session = sessions.get(sessionId);
  if (!session || !session.profilerStarted) return;
  session.profilerStarted = false;
  try {
    const { profile } = await cdp('Profiler.stop', {}, sessionId);
    const entry = targets.find(target => target.sessionId === sessionId);
    const type = entry?.type || 'unknown';
    const index = targets.filter(target => target.profileFile && target.profileFile.startsWith(`${type}-`)).length + 1;
    const name = `${type}-${index}.cpuprofile`;
    await writeFile(join(outDir, name), JSON.stringify(profile) + '\n');
    session.profileFile = name;
    if (entry) entry.profileFile = name;
  } catch {}
}

async function runtimeFrame(currentPage) {
  const handle = await currentPage.locator('#display iframe').elementHandle();
  return handle ? handle.contentFrame() : null;
}

async function callGuest(currentPage, method) {
  try {
    const frame = await runtimeFrame(currentPage);
    if (!frame) throw new Error('runtime iframe is not available');
    const value = await frame.evaluate(name => {
      if (typeof Module === 'undefined' || typeof Module.ccall !== 'function')
        throw new Error('runtime Module export is not available');
      if (name.endsWith('_json')) {
        const result = Module.ccall(name, 'string', [], []);
        if (typeof result === 'string') return result;
        if (result && typeof Module.UTF8ToString === 'function') return Module.UTF8ToString(result);
        throw new Error(name + ' returned no readable text');
      }
      Module.ccall(name, null, [], []);
      return '';
    }, method);
    return { value };
  } catch (error) {
    return { error: error.message || String(error) };
  }
}

async function hasVisibleFrame(currentPage) {
  try {
    return await currentPage.evaluate(() => {
      const display = document.querySelector('#display');
      const frame = display?.querySelector('iframe');
      const canvas = document.querySelector('#vkCanvas') || document.querySelector('#gpuCanvas')
        || frame?.contentDocument?.querySelector('#canvas');
      if (!canvas || !canvas.width || !canvas.height) return false;
      const copy = document.createElement('canvas');
      copy.width = Math.min(canvas.width, 160);
      copy.height = Math.min(canvas.height, 100);
      const context = copy.getContext('2d', { willReadFrequently: true });
      context.drawImage(canvas, 0, 0, copy.width, copy.height);
      const rgba = context.getImageData(0, 0, copy.width, copy.height).data;
      let nonblank = 0;
      for (let at = 0; at < rgba.length; at += 4) if (Math.max(rgba[at], rgba[at + 1], rgba[at + 2]) > 16) nonblank++;
      return nonblank / (copy.width * copy.height) > 0.01;
    });
  } catch { return false; }
}
