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
