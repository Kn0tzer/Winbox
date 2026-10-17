import { chromium } from '@playwright/test';
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';

await waitForBuildIdle();
const slot = await acquireProbeSlot();
let browser, server;
try {
  server = await serve('dist');
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'] });
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  const results = await page.evaluate(async () => {
    const { createEnhancer } = await import('./enhancement.mjs');
    const container = document.createElement('div');
    container.style.cssText = 'position:fixed;top:0;left:0;width:128px;height:128px'; document.body.append(container);
    const source = document.createElement('canvas'); source.width = source.height = 32;
    const webgl = source.getContext('webgl2', { preserveDrawingBuffer: true });
    webgl.clearColor(1, 0, 0, 1); webgl.clear(webgl.COLOR_BUFFER_BIT);
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) throw Error('The WebGPU test adapter is unavailable');
    const device = await adapter.requestDevice(), gpu = document.createElement('canvas'); gpu.width = gpu.height = 32;
    gpu.getContext('webgpu').configure({ device, format: navigator.gpu.getPreferredCanvasFormat() });
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({ colorAttachments: [{ view: gpu.getContext('webgpu').getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 1, g: 0, b: 0, a: 1 } }] });
    pass.end(); device.queue.submit([encoder.finish()]); await device.queue.onSubmittedWorkDone();
    const results = [];
    const capture = document.createElement('canvas'); capture.width = capture.height = 32;
    const captureContext = capture.getContext('2d'); captureContext.drawImage(gpu, 0, 0);
    results.push({ backend: 'webgpu-2d-copy', mode: 'copy', phase: 'immediate', failures: [], pixel: Array.from(captureContext.getImageData(16, 16, 1, 1).data) });
    for (const [backend, canvas] of [['webgpu', gpu], ['webgl2', source]]) for (const mode of ['fsr1', 'sgsr1', 'sharpen']) {
      const failures = [];
      const enhancer = createEnhancer(container, () => canvas, { mode, onError: error => failures.push(error.message) });
      for (const phase of ['immediate', 'after presentation']) {
        if (phase === 'after presentation') await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
        enhancer.render();
        const context = enhancer.canvas.getContext('webgl2'), pixel = new Uint8Array(4);
        context.readPixels(64, 64, 1, 1, context.RGBA, context.UNSIGNED_BYTE, pixel);
        results.push({ backend, mode, phase, failures: [...failures], pixel: Array.from(pixel) });
      }
      enhancer.destroy();
    }
    device.destroy(); container.remove(); return results;
  });
  await mkdir('test-results/enhancement-backends', { recursive: true });
  await writeFile('test-results/enhancement-backends/summary.json', JSON.stringify({ results }, null, 2));
  for (const result of results) {
    assert.deepEqual(result.failures, [], JSON.stringify(result));
    assert.ok(result.pixel[0] > 220 && result.pixel[1] < 25 && result.pixel[2] < 25, JSON.stringify(result));
  }
  await mkdir('test-results/enhancement-backends', { recursive: true });
  await writeFile('test-results/enhancement-backends/summary.json', JSON.stringify({ pass: true, results }, null, 2));
  console.log(JSON.stringify({ pass: true, checks: 'All three enhancements preserve WebGL2 and WebGPU canvas pixels before and after presentation', samples: results.length }));
} finally { await browser?.close(); server?.close(); await slot.release(); }
