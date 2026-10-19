// Replay captured rendering independently of the guest's multi-minute startup.
// FRAME=/absolute/frame.json.gz PROBE_DIST=/absolute/export node ...
import { chromium } from '@playwright/test';
import { readFile, mkdir, writeFile, mkdtemp, readdir, symlink, rm } from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
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
await waitForBuildIdle();
let server, browser, replayRoot;
const logs = [];
const logFile = createWriteStream(join(dir, 'console.log'));
try {
  // Keep large captured frames out of the browser automation transport.
  // A multi-submit Portal capture can exceed 150 MB of JSON.
  replayRoot = await mkdtemp(join(tmpdir(), 'bw-frame-replay-'));
  const exportRoot = resolve(process.env.PROBE_DIST || join(repo, 'dist'));
  for (const name of await readdir(exportRoot)) {
    if (name === '__replay-frame.json') throw Error('Reserved replay fixture name');
    await symlink(join(exportRoot, name), join(replayRoot, name));
  }
  await writeFile(join(replayRoot, '__replay-frame.json'), json);
  server = await serve(replayRoot);
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'] });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const record = line => { logs.push(line); logFile.write(line + '\n'); };
  page.on('console', m => record(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => record(`pageerror: ${e.message}`));
  // Stop automatic runtime boot in this page: rendering needs only WebGPU.
  await page.route(url => url.pathname.endsWith('/wine64-launcher.js'), route => route.fulfill({ body: '', contentType: 'text/javascript' }));
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?replay=1`);
  const result = await page.evaluate(async ({ decode, encode, captureTargets, whiteFragment, passLimit, imageCopy }) => {
    const decodeFrame = (0, eval)(`(${decode})`);
    const encodeFrame = (0, eval)(`(${encode})`);
    let frame = decodeFrame(await (await fetch('/__replay-frame.json')).text());
    if (passLimit > 0) {
      frame = frame.submissions?.[0] || frame;
      frame.passes = frame.passes.slice(0, passLimit);
    }
    if (imageCopy) {
      const submission = frame.submissions?.[0] || frame;
      const { beforePass, ...op } = imageCopy;
      submission.passes.splice(beforePass, 0, { w: submission.width, h: submission.height,
        attachments: [], ops: [{ op: 'imageCopy', ...op }] });
    }
    globalThis.__bwStatsEvery = 1;
    const { createVkRenderer2 } = await import('../vkwebgpu.mjs');
    const canvas = document.createElement('canvas'); canvas.id = 'replayCanvas';
    document.body.replaceChildren(canvas);
    const renderer = await createVkRenderer2(canvas);
    if (whiteFragment) {
      const getShader = renderer.shaders.get.bind(renderer.shaders);
      renderer.shaders.get = (stage, module, options) => stage === 'fragment'
        ? Promise.resolve('@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(1.0); }')
        : getShader(stage, module, options);
    }
    const textures = [];
    if (captureTargets) {
      const createTexture = renderer.device.createTexture.bind(renderer.device);
      renderer.device.createTexture = descriptor => {
        const texture = createTexture(descriptor);
        if (descriptor.usage & GPUTextureUsage.COPY_SRC && texture.sampleCount === 1 &&
            ['bgra8unorm', 'rgba8unorm', 'bgra8unorm-srgb', 'rgba8unorm-srgb'].includes(texture.format))
          textures.push(texture);
        return texture;
      };
    }
    const start = performance.now();
    const outcome = await renderer.render(frame);
    await renderer.device.queue.onSubmittedWorkDone();
    const elapsedMs = performance.now() - start;
    const targets = [];
    for (const texture of textures) {
      const { width, height } = texture;
      const bytesPerRow = Math.ceil(width * 4 / 256) * 256;
      const buffer = renderer.device.createBuffer({ size: bytesPerRow * height, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
      const commands = renderer.device.createCommandEncoder();
      commands.copyTextureToBuffer({ texture }, { buffer, bytesPerRow }, [width, height]);
      renderer.device.queue.submit([commands.finish()]);
      await buffer.mapAsync(GPUMapMode.READ);
      const bytes = new Uint8Array(buffer.getMappedRange());
      const rgba = new Uint8ClampedArray(width * height * 4);
      let nonzeroRgb = 0;
      for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
        const from = y * bytesPerRow + x * 4, to = (y * width + x) * 4;
        const bgra = texture.format.startsWith('bgra');
        rgba[to] = bytes[from + (bgra ? 2 : 0)]; rgba[to + 1] = bytes[from + 1];
        rgba[to + 2] = bytes[from + (bgra ? 0 : 2)]; rgba[to + 3] = 255;
        if (rgba[to] || rgba[to + 1] || rgba[to + 2]) nonzeroRgb++;
      }
      buffer.unmap(); buffer.destroy();
      const image = document.createElement('canvas'); image.width = width; image.height = height;
      image.getContext('2d').putImageData(new ImageData(rgba, width, height), 0, 0);
      targets.push({ width, height, nonzeroRgb, png: image.toDataURL('image/png').split(',')[1] });
    }
    return { outcome, elapsedMs, stats: renderer.snapshot(), targets,
      capturedFrameNo: frame.frameNo, roundtripBytes: encodeFrame(frame).length };
  }, { decode: decodeFrame.toString(), encode: encodeFrame.toString(),
    captureTargets: process.env.CAPTURE_TARGETS === '1', whiteFragment: process.env.DEBUG_FRAGMENT === 'white',
    passLimit: Number(process.env.REPLAY_PASS_LIMIT || 0), imageCopy: process.env.REPLAY_IMAGE_COPY ? JSON.parse(process.env.REPLAY_IMAGE_COPY) : null });
  for (const [index, target] of result.targets.entries()) {
    await writeFile(join(dir, `target-${index}.png`), Buffer.from(target.png, 'base64'));
    delete target.png;
  }
  await page.screenshot({ path: join(dir, 'frame.png') });
  await writeFile(join(dir, 'summary.json'), JSON.stringify({ framePath, ...result, logs }, null, 2));
  console.log(JSON.stringify({ dir, ...result }));
} finally {
  logFile.end();
  await browser?.close(); server?.close();
  if (replayRoot) await rm(replayRoot, { recursive: true, force: true });
  await slot.release();
}
