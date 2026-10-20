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
const jit = process.env.JIT ?? '0';
if (jit !== '0' && jit !== '1') throw new Error('JIT must be 0 or 1');
const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/T/, 'T').slice(0, 15);
const defaultOut = join('test-results/demo-matrix', `${demo}-jit${jit}-${stamp}`);
const outDir = resolve(repo, process.env.OUT || defaultOut);
const longBoot = ['portal', 'celeste', 'gangbeasts', 'chromium'].includes(demo);
const mappedTimeout = Number(process.env.MAPPED_TIMEOUT_S || (longBoot ? 900 : 300));
if (!Number.isFinite(mappedTimeout) || mappedTimeout <= 0) throw new Error('MAPPED_TIMEOUT_S must be a positive number');
const bootOffsets = longBoot ? [5, 30, 60, 120, 240, 360] : [5, 30, 60, 120];
const pixelMethod = 'in-page';

const metricsPath = join(outDir, 'metrics.jsonl');

let slot;
let server;
let browser;
let page;
let build;
let startedAt = 0;
let tClick = null;
let tMapped = null;
let mappedAt = null;
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

const elapsedSeconds = () => Number(((Date.now() - startedAt) / 1000).toFixed(3));

async function main() {
try {
  await mkdir(outDir, { recursive: true });
  await writeFile(metricsPath, '');
  await waitForBuildIdle();
  slot = await acquireProbeSlot();
  build = JSON.parse(await readFile(join(repo, 'dist/runtime/BUILD_INFO.json'), 'utf8'));
  if (!process.env.BW_TEST_BASE_URL) server = await serve('dist');
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
    headless: true,
    args: [
      '--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan',
    ],
  });
  page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
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
    await launchDemoGame(page, demo, { mappedTimeout, onClick: () => { tClick = elapsedSeconds(); } });
  } catch (error) {
    if (error.name !== 'TimeoutError') throw error;
    const timeoutPath = join(outDir, 'timeout.png');
    await page.screenshot({ path: timeoutPath, fullPage: true }).catch(() => {});
    const pixels = await measurePlayerPixels(page).catch(() => ({ nonblankFraction: null, uniqueColors: null, pixelError: 'measurement failed' }));
    screenshots.push({ name: 'timeout.png', atSeconds: elapsedSeconds(), ...pixels });
    const logText = await readLauncherLog(page);
    await writeFile(join(outDir, 'log-tail.txt'), logText.split('\n').slice(-400).join('\n') + '\n');
    await saveLogs(page);
    await writeFile(join(outDir, 'summary.json'), JSON.stringify({
      demo, jit: Number(jit), build, tClick, tMapped: null, clickToMappedSeconds: null,
      screenshots, pageErrorCount: pageErrors.length, consoleErrorCount: countConsoleErrors(),
      lastMetricsSnapshot, lastJitStats, stage: 'not-mapped', pixelMethod, httpErrorCount: httpErrors.length,
    }, null, 2) + '\n');
    console.log(JSON.stringify({ demo, jit: Number(jit), stage: 'not-mapped', outDir: relative(repo, outDir) }));
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
  await page.mouse.click(...await displayCenter());
  await runInputScript();
  sampling = false;
  await samplerPromise;
  await sampleMetrics();
  await saveLogs(page);
  const summary = {
    demo, jit: Number(jit), build, tClick, tMapped,
    clickToMappedSeconds: Number((tMapped - tClick).toFixed(3)),
    screenshots, pageErrorCount: pageErrors.length, consoleErrorCount: countConsoleErrors(),
    lastMetricsSnapshot, lastJitStats, stage: 'mapped', pixelMethod, httpErrorCount: httpErrors.length,
  };
  await writeFile(join(outDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
  console.log(JSON.stringify({ demo, jit: Number(jit), stage: 'mapped', clickToMappedSeconds: summary.clickToMappedSeconds,
    finalScreenshot: screenshots.at(-1), pageErrors: summary.pageErrorCount, consoleErrors: summary.consoleErrorCount,
    outDir: relative(repo, outDir) }));
} finally {
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
  try {
    const frameHandle = await page.locator('#display iframe').elementHandle();
    const runtimeFrame = await frameHandle?.contentFrame();
    if (!runtimeFrame) throw new Error('runtime iframe is not available');
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
