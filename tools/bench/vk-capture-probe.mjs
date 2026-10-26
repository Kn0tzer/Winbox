// Real WebGPU proof for capture placement and exact pixels, independent of Wine.
// OUT=/tmp/... node tools/bench/vk-capture-probe.mjs
import { chromium } from '@playwright/test';
import { resolve, join } from 'node:path';
import { mkdir, writeFile } from 'node:fs/promises';
import assert from 'node:assert/strict';
import { serve } from '../browser/serve.mjs';
import { acquireProbeSlot } from '../../web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '../../web/tests/build-lock.mjs';
import { unpackCaptureImage, writeVkCapture } from './vk-capture-artifact.mjs';
const out = resolve(process.env.OUT || '/tmp/winbox-finish/work-sol-b/capture-selftest');
await mkdir(out, { recursive: true });
await waitForBuildIdle();
const slot = await acquireProbeSlot();
let server, browser;
try {
  server = await serve('dist');
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'] });
  const page = await browser.newPage(); const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  const report = await page.evaluate(async () => {
    const { createVkRenderer2 } = await import('./vkwebgpu.mjs');
    const { createVkFrameCapture } = await import('./vk-frame-capture.mjs');
    const canvas = document.createElement('canvas'); canvas.width = 16; canvas.height = 16; canvas.style.cssText = 'position:fixed;top:0;left:0;width:256px;height:256px;z-index:9999'; document.body.replaceChildren(canvas);
    const capture = createVkFrameCapture();
    const pass = (id, color) => ({ w: 16, h: 16, clearColor: color, clearDepth: 1, ops: [], attachments: [{ imageId: id, format: 37, loadOp: 1, storeOp: 0 }] });
    const frame = { frameNo: 77, width: 16, height: 16, shaders: new Map(), images: new Map(), views: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
      passes: [pass('0000000000000011', [1, 0, 0, 1]), pass('0000000000000012', [0, 1, 0, 1])] };
    capture.arm(77); capture.batch(frame, { swapchains: [{ image: '18' }] });
    const renderer = await createVkRenderer2(canvas, { capture });
    const outcome = await renderer.render(frame); capture.finish(frame, outcome.rendered ? null : outcome.reason);
    await renderer.device.queue.onSubmittedWorkDone();
    return { ...capture.report, webgpuErrors: renderer.errors };
  });
  await writeVkCapture(out, report);
  await page.screenshot({ path: join(out, 'composited.png') });
  assert.deepEqual(errors, []); assert.deepEqual(report.webgpuErrors, []);
  assert.equal(report.status, 'complete'); assert.equal(report.images.length, 4);
  for (const image of report.images) {
    const { rgba } = unpackCaptureImage(image), expected = image.stage === 'pass' && image.pass === 0 ? [255, 0, 0, 255] : [0, 255, 0, 255];
    for (let i = 0; i < rgba.length; i += 4) assert.deepEqual([...rgba.subarray(i, i + 4)], expected);
  }
  await writeFile(join(out, 'verdict.json'), JSON.stringify({ pass: true, checkedImages: 4, checkedPixels: 1024, errors }, null, 2));
  console.log('PASS: 4 same-frame images, 1024 exact RGBA pixels; no browser/WebGPU errors');
} finally {
  await browser?.close();
  if (server) await new Promise(r => server.close(r));
  await slot.release();
}
