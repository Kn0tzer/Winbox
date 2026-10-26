import { extractSourceRange } from '../lib/source-slice.mjs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, mkdtempSync, writeFileSync, rmSync} from 'node:fs';
import {dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('ZIP read and seek cannot change a copy length midway through a read', () => {
  const source = readFileSync(join(repoRoot, 'source/io/fszipopennode.cpp'), 'utf8');
  function method(signature) {
    const start = source.indexOf(signature);
    assert.ok(start >= 0, signature);
    let end = source.indexOf('{', start), depth = 1;
    for (end++; depth && end < source.length; end++) {
      if (source[end] === '{') depth++;
      if (source[end] === '}') depth--;
    }
    return extractSourceRange(source, start, end);
  }
  const dir = mkdtempSync(join(tmpdir(), 'bw-zip-read-seek-'));
  try {
    const harness = String.raw`
#include <cstdint>
#include <cstring>
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>
using U8=uint8_t; using U32=uint32_t; using U64=uint64_t; using S64=int64_t;
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) std::lock_guard<std::recursive_mutex> lock(m)
struct FsZip { std::atomic<U64> statCacheServedBytes{0}; };
struct FsZipNode { std::weak_ptr<FsZip> fsZip; U64 length() { return 2048; } };
struct FsZipOpenNode {
  std::shared_ptr<FsZipNode> zipNode = std::make_shared<FsZipNode>();
  std::recursive_mutex readMutex;
  S64 pos=1024;
  std::vector<U8> cache;
  bool cacheComplete=false;
  static constexpr U64 CACHE_CAP=8*1024*1024;
  std::atomic<bool> reading{false}, seekStarted{false};
  bool ensureStream() { return true; }
  void closeStream() {}
  void positionStream(U64) {}
  U32 readFromStream(U8* dst, U32 len) {
    reading=true;
    while (!seekStarted) std::this_thread::yield();
    // The seek gets a chance to alter pos while the reader owns readMutex.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::memset(dst,42,len); return len;
  }
  S64 getFilePointer();
  S64 seek(S64);
  void reopen();
  U32 readNative(U8*,U32);
};
`;
    const main = String.raw`
int main() {
  FsZipOpenNode node;
  std::thread seek([&] {
    while (!node.reading) std::this_thread::yield();
    node.seekStarted=true;
    assert(node.seek(0)==0);
  });
  U8* bytes=new U8[4];
  U32 count=node.readNative(bytes,4);
  seek.join();
  assert(count==4);
  for(int n=0;n<4;n++) assert(bytes[n]==42);
  assert(node.getFilePointer()==0);
  node.seek(17);node.reopen();assert(node.getFilePointer()==0);
  delete[] bytes;
}
`;
    writeFileSync(join(dir, 'test.cpp'), harness + [
      'S64 FsZipOpenNode::getFilePointer()', 'S64 FsZipOpenNode::seek(S64 pos)',
      'void FsZipOpenNode::reopen()', 'U32 FsZipOpenNode::readNative(U8* buffer, U32 len)',
    ].map(method).join('\n') + main);
    const build = spawnSync('g++', ['-std=c++20', '-fsanitize=address', '-g', '-pthread', join(dir,'test.cpp'), '-o', join(dir,'test')], {encoding:'utf8', timeout:30000});
    assert.equal(build.status,0,build.stderr);
    const run = spawnSync(join(dir,'test'), [], {encoding:'utf8', timeout:5000});
    assert.equal(run.status,0,run.stderr || String(run.error || 'ZIP read failed'));
  } finally { rmSync(dir,{recursive:true,force:true}); }
});
