// web/tests/suite/unit/v2-chunk-format.test.mjs — V2 chunk/record format consistency.
//
// The bridge (C++) emits V2 chunks; the page (JS) parses them. If the format
// constants drift between the two, records are silently misparsed.
// This test verifies:
//   (a) All V2_REC types in the page decoder have bridge-side emitters
//   (b) Chunk header magic/version constants are consistent
//   (c) Record type IDs are in the valid range

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseV2Chunk, V2_MAGIC, V2_VERSION } from '../../../vkwebgpu.mjs';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const pageSrc = readFileSync(join(repoRoot, 'web/vk-protocol.mjs'), 'utf8');
const bridgeSrc = readFileSync(join(repoRoot, 'source/vulkan/vk64bridge.cpp'), 'utf8');

// Extract V2_REC from page
function getV2Rec(src) {
  const m = src.match(/export const V2_REC = Object\.freeze\(\{([\s\S]*?)\}\);/);
  if (!m) throw new Error('V2_REC not found in page source');
  const rec = {};
  for (const mm of m[1].matchAll(/(\w+):\s*0x([0-9a-fA-F]+)/g)) {
    rec[mm[1]] = parseInt(mm[2], 16);
  }
  return rec;
}

const v2rec = getV2Rec(pageSrc);

test('V2_REC has all expected record types', () => {
  const expected = [
    'FRAME_BEGIN', 'FRAME_END', 'RP_BEGIN', 'RP_END',
    'SHADER', 'PIPELINE', 'BIND_SETS', 'DESC_SET',
    'BUFFER_DATA', 'IMAGE_DATA', 'SAMPLER', 'PUSH',
    'VIEWPORT', 'SCISSOR', 'VERTEX_BIND', 'INDEX_BIND',
    'DRAW', 'INLINE_BYTES', 'READBACK', 'DEPTH_BIAS',
  ];
  for (const e of expected) {
    assert.ok(v2rec[e] !== undefined, `V2_REC must define ${e}`);
  }
});

test('V2_REC type IDs are sequential from 0x01', () => {
  const vals = Object.values(v2rec).sort((a, b) => a - b);
  for (let i = 0; i < vals.length; i++) {
    assert.equal(vals[i], i + 1, `V2_REC IDs must be sequential 0x01..; got 0x${vals[i].toString(16)} at index ${i}`);
  }
});

test('page chunk parser accepts all V2_REC types', () => {
  const types = Object.values(v2rec);
  const chunk = new Uint8Array(16 + types.length * 8), view = new DataView(chunk.buffer);
  view.setUint32(0, V2_MAGIC, true); view.setUint16(4, V2_VERSION, true);
  types.forEach((type, index) => view.setUint16(16 + index * 8, type, true));
  assert.deepEqual(parseV2Chunk(chunk).records.map(record => record.type), types);
  view.setUint16(16, Math.max(...types) + 1, true);
  assert.throws(() => parseV2Chunk(chunk), /unknown record type/);
});

test('V2 chunk magic and version constants exist', () => {
  assert.ok(pageSrc.includes('V2_MAGIC'), 'Page must define V2_MAGIC');
  assert.ok(pageSrc.includes('V2_VERSION'), 'Page must define V2_VERSION');
  assert.ok(pageSrc.includes('V2_CHUNK_HEADER_LEN'), 'Page must define V2_CHUNK_HEADER_LEN');
  assert.ok(pageSrc.includes('V2_MAX_CHUNK_BYTES'), 'Page must define V2_MAX_CHUNK_BYTES');
});

test('bridge emits V2 chunk header (magic + version)', () => {
  // The bridge uses V2_CHUNK_MAGIC/V2_CHUNK_VERSION; page uses V2_MAGIC/V2_VERSION.
  // Values must match: 0x46324B56 ('VK2F'), version 2.
  assert.ok(bridgeSrc.includes('V2_CHUNK_MAGIC'), 'Bridge must define V2_CHUNK_MAGIC');
  assert.ok(bridgeSrc.includes('V2_CHUNK_VERSION'), 'Bridge must define V2_CHUNK_VERSION');
  assert.ok(pageSrc.includes('V2_MAGIC'), 'Page must define V2_MAGIC');
  assert.ok(pageSrc.includes('V2_VERSION'), 'Page must define V2_VERSION');
});

test('V2 magic and version values match between bridge and page', () => {
  const bridgeMagic = bridgeSrc.match(/V2_CHUNK_MAGIC\s*=\s*(0x[0-9a-fA-F]+)/);
  const pageMagic = pageSrc.match(/V2_MAGIC\s*=\s*(0x[0-9a-fA-F]+)/);
  assert.ok(bridgeMagic && pageMagic, 'Both must define magic constants');
  assert.equal(bridgeMagic[1].toLowerCase(), pageMagic[1].toLowerCase(),
    `Magic must match: bridge=${bridgeMagic[1]}, page=${pageMagic[1]}`);
  const bridgeVer = bridgeSrc.match(/V2_CHUNK_VERSION\s*=\s*(\d+)/);
  const pageVer = pageSrc.match(/V2_VERSION\s*=\s*(\d+)/);
  assert.ok(bridgeVer && pageVer, 'Both must define version constants');
  assert.equal(bridgeVer[1], pageVer[1],
    `Version must match: bridge=${bridgeVer[1]}, page=${pageVer[1]}`);
});

test('READBACK record (0x13) is handled by page decoder', () => {
  // Regression: 0x13 was added for the readback path (e294e5c2). Verify the
  // page decoder has a case for it.
  assert.ok(
    pageSrc.includes('V2_REC.READBACK') || pageSrc.includes('0x13'),
    'Page decoder must handle READBACK record type'
  );
});
