import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));

test('native sampler emission encodes the SAM1 tail and the decoder reads all state', () => {
  const bridge = readFileSync(join(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');
  const samplerStruct = bridge.match(/struct Sampler : Obj \{[\s\S]*?\n\};/)[0];
  const writePayload = bridge.match(/static void writeSamplerRecordPayload\([\s\S]*?\n\}/)[0];
  assert.equal((bridge.match(/s->anisotropyEnable = ci\.anisotropyEnable/g) || []).length, 2,
    'both 32-bit and 64-bit VkSamplerCreateInfo paths capture anisotropy state');

  const dir = mkdtempSync(join('/tmp/winbox-finish/work-G1/', 'sampler-'));
  try {
    const source = join(dir, 'sampler.cpp'), binary = join(dir, 'sampler');
    writeFileSync(source, `
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using U32=uint32_t; using U64=uint64_t;
struct Obj {};
${samplerStruct}
struct ChunkWriter {
  std::vector<unsigned char> bytes;
  void u32(U32 v) { for (int i=0;i<4;i++) bytes.push_back((unsigned char)(v>>(8*i))); }
  void u64(U64 v) { for (int i=0;i<8;i++) bytes.push_back((unsigned char)(v>>(8*i))); }
  void f32(float v) { U32 bits; std::memcpy(&bits,&v,4); u32(bits); }
};
${writePayload}
int main() {
  Sampler s; s.mag=1; s.min=1; s.mipmap=1; s.addrU=0; s.addrV=3; s.addrW=4;
  s.maxAniso=6; s.anisotropyEnable=1; s.compareEnable=1; s.compareOp=6;
  s.mipLodBias=1.25f; s.minLod=0.5f; s.maxLod=17.0f; s.borderColor=5;
  ChunkWriter writer; writeSamplerRecordPayload(writer,0x5a5a,s);
  std::fwrite(writer.bytes.data(),1,writer.bytes.size(),stdout);
}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Wextra', '-Werror', source, '-o', binary]);
    const payload = new Uint8Array(execFileSync(binary));
    assert.equal(payload.byteLength, 68);
    const frame = decodeV2Frame([
      { type: V2_REC.FRAME_BEGIN, payload: new Uint8Array([1,0,0,0,4,0,0,0,4,0,0,0,0,0,0,0]) },
      { type: V2_REC.SAMPLER, payload },
      { type: V2_REC.FRAME_END, payload: new Uint8Array([1,0,0,0]) },
    ]);
    assert.deepEqual(frame.samplers.get('0000000000005a5a'), {
      mag: 'linear', min: 'linear', mipmap: 'linear',
      addrU: 'repeat', addrV: 'clamp-to-edge', addrW: 'clamp-to-edge',
      maxAnisotropy: 6, anisotropyEnable: true, compareEnable: true, compareOp: 6,
      mipLodBias: 1.25, minLod: 0.5, maxLod: 17, borderColor: 5,
    });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
