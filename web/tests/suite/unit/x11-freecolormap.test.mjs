// web/tests/suite/unit/x11-freecolormap.test.mjs — XFreeColormap must free
// the colormap (or refuse loudly), not silently do nothing.
//
// source/x11/x11common.cpp:x11_FreeColorMap was an empty body with a
// commented-out kpanic: the guest's colormap stayed in the server table
// forever and an unknown id produced no error. XServer now owns removal
// (freeColorMap: the default colormap is a no-op success, a real colormap
// is removed, an unknown id is refused) and the handler maps that to
// Success/BadColor. Both real bodies are compiled against stubs and run.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/x11common.cpp';
const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const handler = extractSourceSlice(source, 'static void x11_FreeColorMap(CPU* cpu) {', '// int XFreeColors(', { sourceName });

const serverSource = readFileSync(new URL('../../../../source/x11/xserver.cpp', import.meta.url), 'utf8');
const freeColorMap = extractSourceSlice(serverSource, 'bool XServer::freeColorMap(U32 id) {', 'void XServer::initVisuals()', { sourceName: 'source/x11/xserver.cpp' });

const x11h = readFileSync(new URL('../../../../source/x11/x11.h', import.meta.url), 'utf8');
const badColor = Number(x11h.match(/#define\s+BadColor\s+(\d+)/)[1]);

test('x11_FreeColorMap is implemented and delegates to the server', () => {
  assert.match(handler, /freeColorMap\(ARG2\)/);
  assert.doesNotMatch(handler, /kpanic/);
});

function compileAndRun(name, code) {
  const dir = mkdtempSync(join(tmpdir(), name));
  try {
    writeFileSync(join(dir, 'check.cpp'), code);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

test('x11_FreeColorMap reports Success for a freed map and BadColor for an unknown one', () => {
  compileAndRun('x11-freecolormap-', `
#include <cstdint>
#include <cstdio>
typedef uint32_t U32;
enum { Success = 0, BadColor = ${badColor} };
struct XServer {
  static XServer* getServer() { static XServer s; return &s; }
  U32 lastFreed = 0;
  bool result = true;
  bool freeColorMap(U32 id) { lastFreed = id; return result; }
};
struct CPU { U32 eax; U32 args[8]; };
#define EAX (cpu->eax)
#define ARG2 (cpu->args[1])
${handler}
int main() {
  CPU cpu{};
  cpu.args[1] = 42;
  x11_FreeColorMap(&cpu);
  XServer* server = XServer::getServer();
  if (server->lastFreed != 42) { std::fprintf(stderr, "freed %u\\n", server->lastFreed); return 1; }
  if (cpu.eax != Success) { std::fprintf(stderr, "eax %u\\n", cpu.eax); return 2; }
  server->result = false;
  x11_FreeColorMap(&cpu);
  if (cpu.eax != BadColor) { std::fprintf(stderr, "eax %u\\n", cpu.eax); return 3; }
  return 0;
}
`);
});

test('XServer::freeColorMap keeps the default, removes real maps, refuses unknown ids', () => {
  compileAndRun('xserver-freecolormap-', `
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
typedef uint32_t U32;
template <typename K, typename V>
struct BHashTable {
  std::map<K, V> m;
  void set(const K& k, const V& v) { m[k] = v; }
  V get(const K& k) const { auto it = m.find(k); return it == m.end() ? V{} : it->second; }
  void remove(const K& k) { m.erase(k); }
};
struct XColorMap { U32 id = 0; };
using XColorMapPtr = std::shared_ptr<XColorMap>;
struct BOXEDWINE_MUTEX {};
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) static_cast<void>(0)
struct XServer {
  BOXEDWINE_MUTEX colorMapMutex;
  BHashTable<U32, XColorMapPtr> colorMaps;
  XColorMapPtr defaultColorMap;
  bool freeColorMap(U32 id);
};
${freeColorMap}
int main() {
  XServer s;
  auto def = std::make_shared<XColorMap>(); def->id = 1;
  s.defaultColorMap = def; s.colorMaps.set(1, def);
  auto cmap = std::make_shared<XColorMap>(); cmap->id = 2;
  s.colorMaps.set(2, cmap);
  if (!s.freeColorMap(1)) { std::fprintf(stderr, "default free refused\\n"); return 1; }
  if (!s.colorMaps.get(1)) { std::fprintf(stderr, "default colormap removed\\n"); return 2; }
  if (!s.freeColorMap(2)) { std::fprintf(stderr, "real free refused\\n"); return 3; }
  if (s.colorMaps.get(2)) { std::fprintf(stderr, "colormap still present\\n"); return 4; }
  if (s.freeColorMap(99)) { std::fprintf(stderr, "unknown id accepted\\n"); return 5; }
  return 0;
}
`);
});
