// Path cache benchmark probe.
// Boots Wine with ?pathcachestats=1, waits for vkboot, then extracts the
// "pathcache:" stats lines from the console log to report hit rate.
// Compares against ?nopathcache=1 for timing.
//
// Run from repo root:
//   TAG=pathcache-bench1 TIMEOUT=1200000 node web/tests/pathcache-bench-probe.mjs
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { mkdir, writeFile } from 'node:fs/promises';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const slot = await acquireProbeSlot();
try {
const tag = process.env.TAG || `pathcache-bench-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/pathcache-${tag}`;
await mkdir(dir, { recursive: true });
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
page.on('pageerror', e => errors.push(e.message));
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const timeout = Number(process.env.TIMEOUT || 1200000);
const noCache = process.env.NOCACHE === '1';
const extraParams = noCache ? '&nopathcache=1' : '&pathcachestats=1';

const t0 = Date.now();
try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=0&persist=0${extraParams}`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  const bootstrapUntil = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) throw new Error('Wine bootstrap sentinel timed out');
    await flush();
    await page.waitForTimeout(500);
  }
  const bootMs = Date.now() - t0;
  console.log(`BOOTSTRAP COMPLETE in ${bootMs}ms`);

  // Let any pending stats flush, then collect pathcache lines
  await page.waitForTimeout(2000);
  await flush();

  const statsLines = logs.filter(l => l.includes('pathcache:'));
  console.log(`Found ${statsLines.length} pathcache stats lines`);
  for (const l of statsLines.slice(-3)) {
    console.log('STATS:', l);
  }

  const summary = {
    passed: true,
    noCache,
    bootMs,
    statsLines: statsLines.slice(-5),
    errors: errors.slice(0, 5),
  };
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('SUMMARY:', JSON.stringify(summary));
} catch (e) {
  console.log('PROBE FAILED:', e.message);
  await flush();
  await writeFile(`${dir}/summary.json`, JSON.stringify({ passed: false, error: e.message }));
} finally {
  await browser.close();
  server.close();
}
} finally {
  slot.release();
}
