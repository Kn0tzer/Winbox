import test from 'node:test';
import assert from 'node:assert/strict';
import { deflateSync } from 'node:zlib';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { screenshotPixels } from '../lib/pixels.mjs';

function pngChunk(type, data) {
  const chunk = Buffer.alloc(12 + data.length);
  chunk.writeUInt32BE(data.length, 0);
  chunk.write(type, 4, 4, 'ascii');
  data.copy(chunk, 8);
  return chunk;
}

function rgbPng(width, height, pixels) {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4);
  header[8] = 8; header[9] = 2;
  const rows = Buffer.alloc(height * (1 + width * 3));
  for (let y = 0; y < height; y++) {
    rows[y * (1 + width * 3)] = 0;
    for (let x = 0; x < width; x++) pixels[y * width + x].copy(rows, y * (1 + width * 3) + 1 + x * 3);
  }
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    pngChunk('IHDR', header), pngChunk('IDAT', deflateSync(rows)), pngChunk('IEND', Buffer.alloc(0)),
  ]);
}

test('screenshot decoder measures non-dark pixels and distinct colors', () => {
  const pixels = [[0, 0, 0], [255, 0, 0], [0, 255, 0], [0, 0, 255]].map(rgb => Buffer.from(rgb));
  const result = screenshotPixels(rgbPng(2, 2, pixels));
  assert.equal(result.nonDarkPixelFraction, 0.75);
  assert.equal(result.distinctColorCount, 4);
});

test('demo matrix uses clipped Chromium screenshots and fails after consecutive blank samples', () => {
  const source = readFileSync(resolve(import.meta.dirname, '../../demo-matrix.mjs'), 'utf8');
  assert.match(source, /currentPage\.screenshot\(\{ type: 'png', clip \}\)/);
  assert.match(source, /screenshotPixels\(await currentPage\.screenshot/);
  assert.match(source, /blankScreenFailureSamples = 6/);
  assert.match(source, /display screenshot stayed blank for/);
  assert.match(source, /snapshot\?\.present\?\.count/);
  assert.doesNotMatch(source, /drawImage\(/, 'WebGPU output must be read from the composited screenshot');
});

function filteredPng(channels, filter) {
  const width = 3, height = 2, stride = width * channels;
  const pixels = Buffer.from(Array.from({ length: stride * height }, (_, i) => (i * 67 + 17) & 255));
  const rows = Buffer.alloc(height * (stride + 1));
  for (let y = 0; y < height; y++) {
    rows[y * (stride + 1)] = filter;
    for (let x = 0; x < stride; x++) {
      const left = x >= channels ? pixels[y * stride + x - channels] : 0;
      const up = y ? pixels[(y - 1) * stride + x] : 0;
      const corner = y && x >= channels ? pixels[(y - 1) * stride + x - channels] : 0;
      const p = left + up - corner;
      const distances = [left, up, corner].map(value => Math.abs(p - value));
      const paeth = [left, up, corner][distances.indexOf(Math.min(...distances))];
      const predictor = [0, left, up, Math.floor((left + up) / 2), paeth][filter];
      rows[y * (stride + 1) + x + 1] = (pixels[y * stride + x] - predictor) & 255;
    }
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width); header.writeUInt32BE(height, 4);
  header[8] = 8; header[9] = channels === 3 ? 2 : 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    pngChunk('IHDR', header), pngChunk('IDAT', deflateSync(rows)), pngChunk('IEND', Buffer.alloc(0))]);
}

test('all Chromium RGB/RGBA PNG row filters reconstruct the same screenshot pixels', () => {
  for (const channels of [3, 4]) {
    const expected = screenshotPixels(filteredPng(channels, 0));
    for (const filter of [1, 2, 3, 4]) assert.deepEqual(screenshotPixels(filteredPng(channels, filter)), expected);
  }
  const blank = screenshotPixels(rgbPng(2, 2, Array.from({ length: 4 }, () => Buffer.alloc(3))));
  assert.equal(blank.nonDarkPixelFraction, 0);
  assert.equal(blank.distinctColorCount, 1);
});

test('invalid screenshot bytes cannot become visible-pixel evidence', () => {
  const valid = filteredPng(3, 0);
  const badSignature = Buffer.from(valid); badSignature[0] = 0;
  const interlaced = Buffer.from(valid); interlaced[28] = 1;
  const wrongSize = Buffer.from(valid); wrongSize.writeUInt32BE(99, 16);
  for (const invalid of [badSignature, interlaced, wrongSize, valid.subarray(0, valid.length - 1)]) {
    assert.throws(() => screenshotPixels(invalid), /screenshot PNG/);
  }
});

async function runPixelGate(images) {
  const { runInNewContext } = await import('node:vm');
  const { extractSourceSlice } = await import('../lib/source-slice.mjs');
  const source = readFileSync(resolve(import.meta.dirname, '../../demo-matrix.mjs'), 'utf8');
  const methods = extractSourceSlice(source, 'async function measurePlayerPixels(', 'function startMetricsSampler()', { sourceName: 'demo-matrix.mjs' });
  let screenshotCount = 0;
  const state = { exitCode: 0 }, waits = [], messages = [];
  const methodsUnderTest = runInNewContext(methods + '; ({ waitForVisibleScreenshotPixels, framesPresentedTotal })', {
    page: {
      locator: () => ({ boundingBox: async () => ({ x: 0, y: 0, width: 3, height: 2 }) }),
      screenshot: async () => images[Math.min(screenshotCount++, images.length - 1)],
      waitForTimeout: async milliseconds => waits.push(milliseconds),
    },
    screenshotPixels, screenshotPixelMethod: 'chromium-screenshot-png-display',
    blankScreenGraceMs: 15000, blankScreenFailureSamples: 6, blankScreenSampleIntervalMs: 5000,
    screenshots: [], elapsedSeconds: () => 0, process: state, console: { error: message => messages.push(message) },
    wasmMetricsLines: [{ snapshot: { present: { count: 2 } } }, { snapshot: { present: { count: 3 } } }],
  });
  return { result: await methodsUnderTest.waitForVisibleScreenshotPixels(), state, waits, messages,
    screenshotCount, framesPresented: methodsUnderTest.framesPresentedTotal() };
}

test('mapped-but-black screenshots fail the actual matrix gate after six samples and grace', async () => {
  const blank = rgbPng(2, 2, Array.from({ length: 4 }, () => Buffer.alloc(3)));
  const run = await runPixelGate([blank]);
  assert.equal(run.result.visible, false);
  assert.equal(run.state.exitCode, 1);
  assert.equal(run.screenshotCount, 6);
  assert.deepEqual(run.waits, [15000, 5000, 5000, 5000, 5000, 5000]);
  assert.match(run.messages[0], /mapped.*blank for 6 consecutive samples/);
  assert.equal(run.framesPresented, 5);
});

test('the matrix accepts real screenshot pixels after transient blank frames', async () => {
  const blank = rgbPng(2, 2, Array.from({ length: 4 }, () => Buffer.alloc(3)));
  const visible = rgbPng(4, 2, Array.from({ length: 8 }, (_, i) => Buffer.from([i * 30, 80, 255])));
  const run = await runPixelGate([blank, blank, visible]);
  assert.equal(run.result.visible, true);
  assert.equal(run.state.exitCode, 0);
  assert.equal(run.screenshotCount, 3);
  assert.equal(run.messages.length, 0);
});
