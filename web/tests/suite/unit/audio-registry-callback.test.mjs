import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('audio enable callbacks can create and release voices without locking the registry recursively', () => {
  const source = readFileSync(new URL('../../../../platform/sdl/kdspaudio.cpp', import.meta.url), 'utf8');
  const start = source.indexOf('void KDspAudio::iterateOpenAudio(');
  assert.ok(start >= 0);
  let end = source.indexOf('{', start), depth = 1;
  for (end++; depth && end < source.length; end++) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
  }
  const implementation = source.slice(start, end);
  const dir = mkdtempSync(join(tmpdir(), 'bw-audio-registry-'));
  try {
    writeFileSync(join(dir, 'test.cpp'), `
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) std::lock_guard<std::mutex> lock(m)
class KDspAudio;
using KDspAudioPtr = std::shared_ptr<KDspAudio>;
struct Entry { std::weak_ptr<KDspAudio> value; };
class KDspAudio {
public:
 static std::mutex mutex;
 static std::vector<Entry> openAudios;
 static int destroyed;
 ~KDspAudio() { std::lock_guard<std::mutex> lock(mutex); ++destroyed; }
 static KDspAudioPtr create() {
  auto value = std::make_shared<KDspAudio>();
  std::lock_guard<std::mutex> lock(mutex); openAudios.push_back({value}); return value;
 }
 static void iterateOpenAudio(std::function<void(KDspAudioPtr&)> callback);
};
std::mutex KDspAudio::mutex;
std::vector<Entry> KDspAudio::openAudios;
int KDspAudio::destroyed;
${implementation}
int main() {
 auto first = KDspAudio::create(); int calls = 0;
 KDspAudio::iterateOpenAudio([&](KDspAudioPtr& voice) {
  assert(voice == first); ++calls;
  auto second = KDspAudio::create(); second.reset(); first.reset();
 });
 assert(calls == 1); assert(KDspAudio::destroyed == 2);
 KDspAudio::iterateOpenAudio([&](KDspAudioPtr&) { ++calls; });
 assert(calls == 1);
}
`);
    execFileSync('c++', ['-std=c++17', '-pthread', join(dir, 'test.cpp'), '-o', join(dir, 'test')]);
    execFileSync(join(dir, 'test'), [], { timeout: 2000 });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
