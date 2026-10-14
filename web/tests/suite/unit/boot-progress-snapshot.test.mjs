import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';

test('a loading-screen label remains owned while a guest advances the boot stage', () => {
  const root = resolve(import.meta.dirname, '../../../..');
  const source = readFileSync(join(root, 'source/kernel/ksystem.cpp'), 'utf8').replaceAll('\r', '');
  const percentType = source.match(/^(.+) KSystem::bootProgressPercent =/m)?.[1];
  assert.ok(percentType);
  const start = source.indexOf(`${percentType} KSystem::bootProgressPercent =`);
  const end = source.indexOf('U32 KSystem::pageSize', start);
  assert.ok(end > start);
  const strings = readFileSync(join(root, 'source/util/bstring.cpp'), 'utf8');
  const original = '    data = s.data;\n    data->incRefCount();';
  assert.ok(strings.includes(original));
  // Pause the real copy constructor between capturing the data pointer and
  // acquiring ownership. This forces the writer's hazardous interleaving.
  const scheduledStrings = strings.replace(original,
    '    data = s.data;\n    extern void bootLabelCopyHook(const BString*);\n    bootLabelCopyHook(&s);\n    data->incRefCount();');
  const dir = mkdtempSync(join(tmpdir(), 'bw64-boot-label-'));
  try {
    writeFileSync(join(dir, 'strings.cpp'), scheduledStrings);
    writeFileSync(join(dir, 'check.cpp'), `
#include "boxedwine.h"
#include <future>
#include <cstring>
#include <deque>
${source.slice(start, end)}
void kpanic(const char*) { std::abort(); }
std::promise<void> captured, writerScheduled;
auto writerGate = writerScheduled.get_future();
void bootLabelCopyHook(const BString* from) {
  if (from != &KSystem::bootProgressLabel) return;
  captured.set_value();
  writerGate.wait();
}
int main() {
  KSystem::bootProgressLabel = BString::copy("initial stage");
  KSystem::bootProgressPercent = 0;
  std::thread writer([] {
    captured.get_future().wait();
    std::unique_lock<std::mutex> probe(g_bootLogMutex, std::try_to_lock);
    if (probe.owns_lock()) {
      probe.unlock();
      // Without a snapshot lock, complete the replacement before the reader
      // acquires the old buffer's reference. ASan must reject this path.
      KSystem::noteBootStage(B("wine64"));
      writerScheduled.set_value();
    } else {
      // The snapshot owns the lock: release the reader, then the real stage
      // writer waits for it rather than freeing its captured buffer.
      writerScheduled.set_value();
      KSystem::noteBootStage(B("wine64"));
    }
  });
  BString label = ${source.includes('BString KSystem::getBootProgressLabel()') ? 'KSystem::getBootProgressLabel()' : 'KSystem::bootProgressLabel'};
  writer.join();
  assert(std::strcmp(label.c_str(), "initial stage") == 0);
  assert(KSystem::bootProgressPercent == 10);
}
`);
    execFileSync('c++', ['-std=c++20', '-pthread', '-fsanitize=address', '-fno-omit-frame-pointer',
      '-ffunction-sections', '-fdata-sections', '-I' + join(root, 'include'), '-I' + join(root, 'source/util'),
      '-I' + join(root, 'lib/simde'), join(dir, 'check.cpp'), join(dir, 'strings.cpp'),
      '-Wl,--gc-sections', '-o', join(dir, 'check')], { stdio: 'pipe', timeout: 30000 });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe', timeout: 10000,
      env: { ...process.env, ASAN_OPTIONS: 'detect_leaks=1' } });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
