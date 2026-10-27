import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';

test('v2 preserves indexed and nonindexed draw arguments even when legacy capture exists', () => {
  const source = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const submit = extractSourceRange(source, source.indexOf('void serializeSubmitV2('));
  const first = submit.indexOf('                case CMD_DRAW:');
  const tail = submit.slice(first);
  const end = tail.indexOf('                case ', tail.indexOf('                case CMD_DRAW_INDEXED:') + 1);
  const cases = tail.slice(0, end);
  const dir = mkdtempSync(join(tmpdir(), 'v2-draw-contract-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <vector>
using U32=uint32_t; using U64=uint64_t;
enum {CMD_DRAW, CMD_DRAW_INDEXED, V2_DRAW};
struct Cmd {int kind;U64 a=6,b=1,c=2,d=3;U32 capCount=6;struct {int32_t ox=-1;} copy;};
struct ChunkWriter {std::vector<U32> values;void u32(U32 x){values.push_back(x);}void u8(U32 x){values.push_back(x);}void u16(U32 x){values.push_back(x);}};
enum {K_PIPELINE=7};
struct Pipeline {U64 gs=0;bool gsServe=false;};
static Pipeline g_pipe;
template<typename T> T* objOf(U64, int, const char*) { return reinterpret_cast<T*>(&g_pipe); }
static void vk64Unsupported(const char*, const char*) {}
std::vector<U32> run(Cmd k) {
  U32 nDraw=0;size_t v2DrawIdx=0;U64 v2RepVsHash=1;std::vector<U32> v2DrawVtxOff{12};U64 activePipe=1;
  ChunkWriter result;
  auto emit=[&](int,auto write){write(result);};
  switch(k.kind) {${cases}}
  return result.values;
}
int main(){
  Cmd indexed{CMD_DRAW_INDEXED};
  assert((run(indexed)==std::vector<U32>{0,1,0,3,1,0,0,6,2,UINT32_MAX}));
  Cmd plain{CMD_DRAW};
  assert((run(plain)==std::vector<U32>{6,1,2,3,0,0,0,0,0,0}));
  indexed.capCount=0;plain.capCount=0;
  assert((run(indexed)==std::vector<U32>{0,1,0,3,1,0,0,6,2,UINT32_MAX}));
  assert((run(plain)==std::vector<U32>{6,1,2,3,0,0,0,0,0,0}));
  // Geometry disposition (gangbeasts lane): a draw under a geometry-stage
  // pipeline whose stage is not strip-serveable serialises NOTHING (the
  // bridge logs geometry-stage-draw); a serveable one keeps its arguments.
  g_pipe.gs=1;g_pipe.gsServe=false;
  assert(run(plain).empty());
  assert(run(indexed).empty());
  g_pipe.gsServe=true;
  assert((run(plain)==std::vector<U32>{6,1,2,3,0,0,0,0,0,0}));
  assert((run(indexed)==std::vector<U32>{0,1,0,3,1,0,0,6,2,UINT32_MAX}));
}
`);
    execFileSync('c++', ['-std=c++17', '-include', 'cstddef', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
    assert.ok(cases);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
