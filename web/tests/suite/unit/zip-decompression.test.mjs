// web/tests/suite/unit/zip-decompression.test.mjs — Zip decompression anti-thrash.
//
// Verifies the per-node decompression stream + prefix-cache design:
//   (a) each FsZipOpenNode owns a PRIVATE unzFile stream (no shared stream)
//   (b) each node keeps a decompressed PREFIX CACHE (backward seeks = memcpy)
//   (c) the old shared-stream machinery (setupZipRead, lastZipOffset,
//       FsZip::readMutex) is GONE from the read path
//   (d) readNative no longer calls setupZipRead
//
// Background: one global unzFile per zip + one readMutex serialized ALL reads,
// and the loader's seek-heavy pattern (52B/56B reads with backward seeks)
// triggered close->reopen->decompress-from-start on every backward seek,
// decompressing each DLL 3-5x at Wine startup.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const zipH = readFileSync(join(repoRoot, 'source/io/fszip.h'), 'utf8');
const zipCpp = readFileSync(join(repoRoot, 'source/io/fszip.cpp'), 'utf8');
const openNodeH = readFileSync(join(repoRoot, 'source/io/fszipopennode.h'), 'utf8');
const openNodeCpp = readFileSync(join(repoRoot, 'source/io/fszipopennode.cpp'), 'utf8');

test('FsZipOpenNode has a private decompression stream', () => {
  assert.match(openNodeH, /unzFile\s+stream\s*=\s*nullptr/,
    'FsZipOpenNode must own a private unzFile stream (per-node, not shared)');
});

test('FsZipOpenNode has a decompressed prefix cache', () => {
  assert.match(openNodeH, /std::vector<U8>\s+cache/,
    'FsZipOpenNode must have a decompressed prefix cache');
  assert.match(openNodeH, /CACHE_CAP/,
    'prefix cache must have a size cap');
});

test('FsZipOpenNode has a per-node read mutex', () => {
  assert.match(openNodeH, /BOXEDWINE_MUTEX\s+readMutex/,
    'FsZipOpenNode must have its own readMutex (not the global FsZip one)');
  assert.match(openNodeCpp, /return &this->readMutex/,
    'getReadMutex() must return the per-node mutex');
});

test('FsZip exposes openEntryStream for private streams', () => {
  assert.match(zipH, /unzFile\s+openEntryStream\(U64 zipOffset\)/,
    'FsZip must expose openEntryStream() for per-node streams');
  assert.match(zipCpp, /unzFile FsZip::openEntryStream\(U64 zipOffset\)/,
    'FsZip::openEntryStream must be implemented');
});

test('shared-stream machinery is removed', () => {
  for (const [name, src] of [['fszip.h', zipH], ['fszip.cpp', zipCpp]]) {
    assert.doesNotMatch(src, /lastZipOffset/,
      `${name} must not contain lastZipOffset (shared-stream state)`);
    assert.doesNotMatch(src, /lastReadOwner/,
      `${name} must not contain lastReadOwner (shared-stream state)`);
    assert.doesNotMatch(src, /setupZipRead/,
      `${name} must not contain setupZipRead (shared-stream repositioning)`);
  }
  assert.doesNotMatch(zipH, /BOXEDWINE_MUTEX readMutex/,
    'fszip.h must not have the global readMutex');
});

test('readNative does not use the shared stream', () => {
  assert.doesNotMatch(openNodeCpp, /setupZipRead/,
    'readNative must not call setupZipRead');
  assert.doesNotMatch(openNodeCpp, /fsZip->zipfile/,
    'readNative must not touch the shared FsZip::zipfile stream');
  assert.doesNotMatch(openNodeCpp, /lastZipFileOffset/,
    'readNative must not use shared lastZipFileOffset');
});

test('readNative serves backward seeks from cache', () => {
  // In cached mode, the stream only extends the cache forward; reads are
  // memcpy from cache. Verify the memcpy-from-cache path exists.
  assert.match(openNodeCpp, /memcpy\(buffer, this->cache\.data\(\)/,
    'readNative must memcpy from the prefix cache');
});

test('stream is closed when cache completes', () => {
  assert.match(openNodeCpp, /cacheComplete\s*=\s*true/,
    'cacheComplete flag must be set when the entry is fully cached');
  assert.match(openNodeCpp, /closeStream\(\)/,
    'stream must be closed when no longer needed');
});

test('decompression stats are tracked', () => {
  assert.match(zipH, /statBytesDecompressed/,
    'FsZip must track decompressed bytes');
  assert.match(zipH, /statCacheServedBytes/,
    'FsZip must track cache-served bytes');
  assert.match(openNodeCpp, /statBytesDecompressed\s*\+=/,
    'readFromStream must count decompressed bytes');
});
