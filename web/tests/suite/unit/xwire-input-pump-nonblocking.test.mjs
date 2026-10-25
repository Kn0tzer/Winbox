import { extractSourceRange } from '../lib/source-slice.mjs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, mkdtempSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('main-thread input pump defers busy guest connections and flushes under one lock', () => {
  const header = readFileSync(join(repoRoot, 'source/x11wire/xwireconnection.h'), 'utf8');
  const start = header.indexOf('void pumpInputAndFlush()');
  assert.ok(start >= 0);
  let end = header.indexOf('{', start), depth = 1;
  for (end++; depth && end < header.length; end++) {
    if (header[end] === '{') depth++;
    if (header[end] === '}') depth--;
  }
  const method = extractSourceRange(header, start, end);
  const dir = mkdtempSync(join(tmpdir(), 'bw-input-pump-'));
  try {
    writeFileSync(join(dir, 'test.cpp'), `
#include <mutex>
#include <thread>
#include <future>
#include <chrono>
#include <cstdio>
struct Connection {
  std::recursive_mutex ioMutex;
  int delivered = 0, flushed = 0;
  void deliverInputEvents() { std::lock_guard<std::recursive_mutex> lock(ioMutex); delivered++; }
  void flushReplies() { std::lock_guard<std::recursive_mutex> lock(ioMutex); flushed++; }
  ${method}
};
int main() {
  Connection c;
  std::promise<void> busy;
  auto ready = busy.get_future();
  std::thread guest([&] {
    std::lock_guard<std::recursive_mutex> lock(c.ioMutex);
    busy.set_value();
    // A guest GL request can wait for the platform thread while owning ioMutex.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  });
  ready.wait();
  const auto start = std::chrono::steady_clock::now();
  c.pumpInputAndFlush();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const bool deferred = elapsed < std::chrono::milliseconds(200);
  guest.join();
  if (!deferred || c.delivered || c.flushed) {
    std::fprintf(stderr, "busy guest blocked the platform input pump\\n"); return 1;
  }
  c.pumpInputAndFlush();
  if (c.delivered != 1 || c.flushed != 1) return 2;
  // Guest onData also invokes the method while already owning this recursive lock.
  { std::lock_guard<std::recursive_mutex> lock(c.ioMutex); c.pumpInputAndFlush(); }
  return c.delivered == 2 && c.flushed == 2 ? 0 : 3;
}
`);
    const build = spawnSync('g++', ['-std=c++20', '-pthread', join(dir, 'test.cpp'), '-o', join(dir, 'test')], {encoding: 'utf8', timeout: 30000});
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'test'), [], {encoding: 'utf8', timeout: 5000});
    assert.equal(run.status, 0, run.stderr || String(run.error || 'input pump failed'));
  } finally { rmSync(dir, {recursive: true, force: true}); }
});
