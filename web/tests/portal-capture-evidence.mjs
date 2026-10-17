// Attach to an isolated Portal probe launched with --remote-debugging-port.
// Requires Tesseract. Usage: node web/tests/portal-capture-evidence.mjs [output-dir]
import { chromium } from '@playwright/test';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { hasPortalMenu, summarizeRenderedFrames } from './portal-evidence.mjs';

const repo = process.env.PORTAL_REPO ? resolve(process.env.PORTAL_REPO) : resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const dir = resolve(process.argv[2] || `${repo}/test-results/portal-evidence-${Date.now()}`);
await mkdir(dir, { recursive: true });
const browser = await chromium.connectOverCDP(`http://127.0.0.1:${process.env.DEBUG_PORT || 9227}`);
const runFile = promisify(execFile);
let result = { pass: false, menuVerified: false };
try {
  const page = browser.contexts()[0].pages()[0];
  const cdp = await browser.newBrowserCDPSession();
  const provenance = JSON.parse(await readFile(`${repo}/dist/runtime/BUILD_INFO.json`, 'utf8'));
  const browserVersion = await cdp.send('Browser.getVersion');
  const processUsage = async () => {
    const { processInfo } = await cdp.send('SystemInfo.getProcessInfo');
    const processes = await Promise.all(processInfo.map(async p => {
      const status = await readFile(`/proc/${p.id}/status`, 'utf8').catch(() => '');
      return { ...p, rssKiB: Number(/^VmRSS:\s+(\d+)/m.exec(status)?.[1] || 0) };
    }));
    return { timestampMs: performance.now(), processes };
  };
  const snapshot = () => page.evaluate(async () => {
    const r = globalThis.bwVkRenderer2;
    if (r && !globalThis.__portalEvidence) {
      const state = globalThis.__portalEvidence = { samples: [], failures: [] };
      const render = r.render.bind(r);
      r.render = async frame => {
        const start = performance.now();
        try {
          const outcome = await render(frame);
          if (outcome?.rendered) {
            state.samples.push({ frame: frame.frameNo, start, end: performance.now() });
            if (state.samples.length > 4096) state.samples.shift();
          }
          return outcome;
        } catch (e) {
          state.failures.push({ frame: frame.frameNo, start, end: performance.now(), error: String(e) });
          if (state.failures.length > 100) state.failures.shift();
          throw e;
        }
      };
    }
    const adapter = await navigator.gpu?.requestAdapter();
    return { now: performance.now(), timeOrigin: performance.timeOrigin,
      measurement: globalThis.__portalEvidence, stats: globalThis.bwVkStats?.(),
      adapter: adapter?.info && {vendor: adapter.info.vendor, architecture: adapter.info.architecture,
        device: adapter.info.device, description: adapter.info.description},
      jsHeapBytes: performance.memory?.usedJSHeapSize };
  });
  let menuStart, usageStart, text = '';
  const deadline = Date.now() + 20 * 60 * 1000;
  while (Date.now() < deadline) {
    const snap = await snapshot();
    if ((snap.stats?.v2?.rendered || 0) > 0) {
      await page.screenshot({ path: `${dir}/portal-candidate.png` });
      ({ stdout: text } = await runFile('tesseract', [`${dir}/portal-candidate.png`, 'stdout', '--psm', '11']));
      if (hasPortalMenu(text)) {
        if (menuStart === undefined) {
          menuStart = snap.now; usageStart = await processUsage();
          console.log('MENU VERIFIED; measuring at least 60 seconds');
        } else if (snap.now - menuStart >= 60000) {
          const usageEnd = await processUsage();
          const initial = new Map(usageStart.processes.map(p => [p.id, p.cpuTime]));
          const cpuSeconds = usageEnd.processes.reduce((sum, p) => sum + Math.max(0, p.cpuTime - (initial.get(p.id) ?? p.cpuTime)), 0);
          await page.screenshot({ path: `${dir}/portal-main-menu-page.png` });
          await page.locator('#vkCanvas').screenshot({ path: `${dir}/portal-main-menu.png` });
          const timing = summarizeRenderedFrames(snap.measurement.samples, menuStart, snap.now);
          result = { pass: timing.renderedFrames > 0, menuVerified: true, capturedAtUtc: new Date().toISOString(),
            ...timing, bootToMenuSeconds: menuStart / 1000, menuText: text.trim(),
            screenshot: 'portal-main-menu.png', pageScreenshot: 'portal-main-menu-page.png',
            adapter: snap.adapter, browser: browserVersion.product, provenance,
            aggregateCpuPercent: 100 * cpuSeconds / ((usageEnd.timestampMs - usageStart.timestampMs) / 1000),
            aggregateProcessRssMiB: usageEnd.processes.reduce((sum, p) => sum + p.rssKiB, 0) / 1024,
            jsHeapMiB: snap.jsHeapBytes / 1048576,
            rejectedDuringMeasurement: snap.measurement.failures.filter(s => s.start >= menuStart).length,
            renderer: snap.stats?.v2, notes: ['FPS counts successfully replayed game frames; excludes browser animation callbacks.',
              'Render times include WebGPU encoding, shader work, and optional pixel readback.',
              'Process RSS sums all isolated Chromium processes and includes shared mappings.',
              'CPU percent uses CDP cumulative CPU seconds: https://chromedevtools.github.io/devtools-protocol/tot/SystemInfo/#type-ProcessInfo'] };
          await writeFile(`${dir}/frame-samples.json`, JSON.stringify({ menuStart, ...snap }, null, 2));
          break;
        }
      }
    }
    console.log(JSON.stringify({ menuVerified: menuStart !== undefined, rendered: snap.stats?.v2?.rendered || 0,
      rejected: snap.stats?.v2?.rejected || 0, measuringSeconds: menuStart === undefined ? 0 : (snap.now - menuStart) / 1000 }));
    await page.waitForTimeout(15000);
  }
} catch (e) { result.error = String(e.stack || e); }
finally {
  await writeFile(`${dir}/performance.json`, JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result));
  await browser.close();
}
if (!result.pass) process.exitCode = 1;
