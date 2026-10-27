// Gang Beasts visual-proof probe (lane: gb-visual-proof).
// Purpose: compositor-independent evidence about what GB's presented frames
// contain and whether the page tier keeps rendering after the first
// 'v2 frame encoding rejected'. Polls window.bwVkStats() (the vk tier lives
// in the shell page) with __bwStatsEvery=1 so litPixels/paintedPixels are
// refreshed from the present-texture readback on EVERY rendered frame.
// Phases: wait for page-tier renders -> stats series -> input (post-title)
// -> optional runtime-armed frame capture (LAST, so a capture stall cannot
// eat the primary evidence).
import { chromium } from '@playwright/test';
import { appendFile, mkdir, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { writeVkCapture } from '../../tools/bench/vk-capture-artifact.mjs';
import { applyJitSetting, launchDemoGame } from './demo-matrix.mjs';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
process.chdir(repo);
const outDir = resolve(repo, process.env.OUT || 'test-results/gb-visual/probe');
const doCapture = process.env.GB_CAPTURE === '1';
const startedAt = Date.now();
const elapsed = () => (Date.now() - startedAt) / 1000;
const consoleLines = [];
const series = [];
let slot, server, browser, page;

async function statsSnap() {
  return page.evaluate(() => {
    try {
      const s = window.bwVkStats?.();
      if (!s) return null;
      return {
        rendered: s.v2?.rendered ?? null, rejected: s.v2?.rejected ?? null,
        framesReceived: s.v2?.framesReceived ?? null, droppedConsumer: s.v2?.droppedConsumer ?? null,
        litPixels: s.litPixels ?? null, paintedPixels: s.paintedPixels ?? null,
        totalPixels: s.totalPixels ?? null, width: s.width ?? null, height: s.height ?? null,
        lastFrame: s.last?.frame ?? null, lastRendered: s.last?.rendered ?? null,
        unsupported: s.unsupported ?? null,
        captureStatus: window.bwVkCapture?.report?.status ?? null,
      };
    } catch (e) { return { error: String(e) }; }
  }).catch(e => ({ error: String(e) }));
}

async function pollSeries(seconds, tag) {
  const end = Date.now() + seconds * 1000;
  let last = null;
  while (Date.now() < end) {
    const snap = await statsSnap();
    const row = { t: +elapsed().toFixed(1), tag, ...snap };
    series.push(row);
    await appendFile(join(outDir, 'stats-series.jsonl'), JSON.stringify(row) + '\n').catch(() => {});
    last = snap;
    await page.waitForTimeout(5000);
  }
  return last;
}

async function main() {
  await mkdir(outDir, { recursive: true });
  await writeFile(join(outDir, 'stats-series.jsonl'), '');
  await waitForBuildIdle();
  slot = await acquireProbeSlot();
  server = await serve(process.env.BW_TEST_DIST || 'dist');
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
    headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
  });
  page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  await page.addInitScript(() => { window.__bwStatsEvery = 1; });
  page.on('console', msg => {
    consoleLines.push(`[${elapsed().toFixed(1)}s] ${msg.type()}: ${msg.text()}`);
  });
  page.on('pageerror', err => consoleLines.push(`[${elapsed().toFixed(1)}s] pageerror: ${err.stack || err.message}`));
  const baseUrl = `http://127.0.0.1:${server.address().port}/`;
  await page.goto(baseUrl, { waitUntil: 'domcontentloaded' });
  await applyJitSetting(page, '1');
  await launchDemoGame(page, 'gangbeasts', { mappedTimeout: 1200 });
  consoleLines.push(`[${elapsed().toFixed(1)}s] probe: window mapped`);

  // Phase W: wait for page-tier renders to start flowing.
  let snap = null;
  const wEnd = Date.now() + 1200 * 1000;
  while (Date.now() < wEnd) {
    snap = await statsSnap();
    const row = { t: +elapsed().toFixed(1), tag: 'wait', ...snap };
    series.push(row);
    await appendFile(join(outDir, 'stats-series.jsonl'), JSON.stringify(row) + '\n').catch(() => {});
    if ((snap?.rendered ?? 0) >= 3) break;
    await page.waitForTimeout(5000);
  }
  const firstRendered = series.find(r => (r.rendered ?? 0) > 0) || null;

  // Phase S: passive series (title rendering, evolution).
  await pollSeries(240, 'series');

  // Phase I: input AFTER presents are flowing (matrix sends it during load).
  const preInput = await statsSnap();
  const box = await page.locator('#display').boundingBox();
  if (box) await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
  await page.waitForTimeout(1000);
  await page.keyboard.press('Enter');
  await page.waitForTimeout(4000);
  await page.keyboard.press('Enter');
  await page.waitForTimeout(9000);
  await page.keyboard.down('ArrowRight');
  await page.waitForTimeout(1000);
  await page.keyboard.up('ArrowRight');
  await page.keyboard.press('Space');
  await page.screenshot({ path: join(outDir, 'after-input.png'), fullPage: true }).catch(() => {});
  await pollSeries(120, 'post-input');
  const postInput = await statsSnap();

  // Phase C (optional): arm a capture for the next presentation and give it
  // far longer than demo-matrix does to finish its readbacks.
  let captureOutcome = null;
  if (doCapture) {
    await page.evaluate(() => { window.bwVkCapture?.arm(null); });
    const cEnd = Date.now() + 420 * 1000;
    while (Date.now() < cEnd) {
      const s = await statsSnap();
      const row = { t: +elapsed().toFixed(1), tag: 'capture-wait', ...s };
      series.push(row);
      await appendFile(join(outDir, 'stats-series.jsonl'), JSON.stringify(row) + '\n').catch(() => {});
      if (s?.captureStatus && s.captureStatus !== 'rendering') { captureOutcome = s.captureStatus; break; }
      await page.waitForTimeout(5000);
    }
    const report = await page.evaluate(() => window.bwVkCapture?.report || null).catch(() => null);
    if (report) await writeVkCapture(join(outDir, 'vk-capture'), report).catch(e => { captureOutcome = 'export-failed: ' + e.message; });
  }

  await page.screenshot({ path: join(outDir, 'final.png'), fullPage: true }).catch(() => {});
  const summary = {
    firstRendered, preInput, postInput, captureOutcome,
    lastSeries: series.at(-1) || null,
    samplerWarnings: consoleLines.filter(l => l.includes('Non-comparison sampler')).length,
    rejectedFrames: consoleLines.filter(l => l.includes('v2 frame encoding rejected')).length,
    firstSamplerWarningAt: consoleLines.find(l => l.includes('Non-comparison sampler'))?.slice(0, 12) || null,
    firstRejectedAt: consoleLines.find(l => l.includes('v2 frame encoding rejected'))?.slice(0, 12) || null,
  };
  await writeFile(join(outDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
  await writeFile(join(outDir, 'console.txt'), consoleLines.join('\n') + '\n');
  console.log(JSON.stringify(summary));
}

try { await main(); }
finally {
  await writeFile(join(outDir, 'console.txt'), consoleLines.join('\n') + '\n').catch(() => {});
  await browser?.close().catch(() => {});
  if (server) await new Promise(r => server.close(r)).catch(() => {});
  await slot?.release();
}
