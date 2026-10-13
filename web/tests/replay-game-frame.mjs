// Replay captured rendering independently of the guest's multi-minute startup.
// FRAME=/absolute/frame.json.gz PROBE_DIST=/absolute/export node ...
import { chromium } from '@playwright/test';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { gunzipSync } from 'node:zlib';
import { join, resolve } from 'node:path';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { encodeFrame, decodeFrame } from './game-frame-codec.mjs';

const repo = resolve(import.meta.dirname, '../..');
const framePath = resolve(process.env.FRAME);
const bytes = await readFile(framePath);
const json = framePath.endsWith('.gz') ? gunzipSync(bytes).toString() : bytes.toString();
const tag = process.env.TAG || `replay-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw Error('Invalid TAG');
const dir = join(repo, 'test-results', tag);
await mkdir(dir, { recursive: true });
const slot = await acquireProbeSlot();
let server, browser;
const logs = [];
try {
  server = await serve(resolve(process.env.PROBE_DIST || join(repo, 'dist')));
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'] });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => logs.push(`pageerror: ${e.message}`));
  // Stop automatic runtime boot in this page: rendering needs only WebGPU.
  await page.route('**/wine64-launcher.js', route => route.fulfill({ body: '', contentType: 'text/javascript' }));
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?replay=1`);
  const result = await page.evaluate(async ({ json, decode, encode }) => {
    const decodeFrame = (0, eval)(`(${decode})`);
    const encodeFrame = (0, eval)(`(${encode})`);
    const frame = decodeFrame(json);
    globalThis.__bwStatsEvery = 1;
    const { createVkRenderer2 } = await import('../vkwebgpu.mjs');
    const canvas = document.createElement('canvas'); canvas.id = 'replayCanvas';
    document.body.replaceChildren(canvas);
    const renderer = await createVkRenderer2(canvas);
    const start = performance.now();
    const outcome = await renderer.render(frame);
    await renderer.device.queue.onSubmittedWorkDone();
    return { outcome, elapsedMs: performance.now() - start, stats: renderer.snapshot(),
      capturedFrameNo: frame.frameNo, roundtripBytes: encodeFrame(frame).length };
  }, { json, decode: decodeFrame.toString(), encode: encodeFrame.toString() });
  await page.screenshot({ path: join(dir, 'frame.png') });
  await writeFile(join(dir, 'summary.json'), JSON.stringify({ framePath, ...result, logs }, null, 2));
  console.log(JSON.stringify({ dir, ...result }));
} finally {
  await writeFile(join(dir, 'console.log'), logs.join('\n'));
  await browser?.close(); server?.close(); await slot.release();
}
