import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { decodeV2Frame, V2_REC, createVkRenderer2 } from '../../../vkwebgpu.mjs';

test('native transfer recording preserves subresources and rejects empty or negative regions', () => {
  const source = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const structure = source.match(/struct ImageCopy \{[\s\S]*?\n\};/)[0];
  const method = source.match(/void recordImageCopy\([\s\S]*?\n\}/)[0];
  const directory = mkdtempSync(join(tmpdir(), 'native-image-copy-'));
  try {
    writeFileSync(join(directory, 'check.cpp'), `
#include <cstdint>
#include <vector>
#include <cassert>
using U32=uint32_t;using U64=uint64_t;
const U32 VK64_MAX_CMDS=64;enum {CMD_COPY_IMAGE};
${structure}
struct Cmd{U32 kind;ImageCopy imageCopy;};struct CmdBuf{std::vector<Cmd> cmds;};
void pushCmd(CmdBuf* cb,const Cmd& c){cb->cmds.push_back(c);}
${method}
int main(){CmdBuf cb;
recordImageCopy(&cb,1,2,3,4,5,6,7,8,0,9,10,0,11,12,1,2);
assert(cb.cmds.size()==2);const auto& a=cb.cmds[0].imageCopy;const auto& b=cb.cmds[1].imageCopy;
assert(a.src==1 && a.dst==2 && a.srcMip==3 && a.srcLayer==4 && a.dstMip==5 && a.dstLayer==6);
assert(b.srcLayer==5 && b.dstLayer==7 && a.srcOrigin[0]==7 && a.srcOrigin[1]==8 && a.dstOrigin[0]==9 && a.dstOrigin[1]==10);
assert(a.extent[0]==11 && a.extent[1]==12 && a.extent[2]==1);
recordImageCopy(&cb,1,2,0,0,0,0,-1,0,0,0,0,0,1,1,1,1);
recordImageCopy(&cb,1,2,0,0,0,0,0,0,0,0,0,0,0,1,1,1);assert(cb.cmds.size()==2);
}
`);
    execFileSync('c++', ['-std=c++17', join(directory, 'check.cpp'), '-o', join(directory, 'check')]);
    execFileSync(join(directory, 'check'));
  } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('image transfers keep their ordering between render passes', () => {
  const header = new Uint8Array(16), end = new Uint8Array(4), copy = new Uint8Array(68);
  new DataView(header.buffer).setUint32(4, 16, true); new DataView(header.buffer).setUint32(8, 16, true);
  const v = new DataView(copy.buffer); v.setBigUint64(0, 1n, true); v.setBigUint64(8, 2n, true);
  v.setUint32(56, 16, true); v.setUint32(60, 16, true); v.setUint32(64, 1, true);
  const f = decodeV2Frame([{ type: V2_REC.FRAME_BEGIN, payload: header },
    { type: 0x14, payload: copy }, { type: V2_REC.FRAME_END, payload: end }]);
  assert.equal(f.passes[0].attachments.length, 0);
  assert.deepEqual(f.passes[0].ops[0], { op: 'imageCopy', srcImage: '0000000000000001', dstImage: '0000000000000002',
    srcMip: 0, srcLayer: 0, dstMip: 0, dstLayer: 0, srcOrigin: [0, 0, 0], dstOrigin: [0, 0, 0], extent: [16, 16, 1] });
  v.setUint32(56, 0, true);
  assert.throws(() => decodeV2Frame([{ type: V2_REC.FRAME_BEGIN, payload: header },
    { type: 0x14, payload: copy }, { type: V2_REC.FRAME_END, payload: end }]), /image copy/);
});

test('a rendered image is copied between ordered render passes', async () => {
  const names = ['navigator', 'GPUBufferUsage', 'GPUTextureUsage', 'GPUShaderStage', 'GPUMapMode'];
  const saved = new Map(names.map(k => [k, Object.getOwnPropertyDescriptor(globalThis, k)]));
  const events = [], textures = [];
  const device = {
    lost: new Promise(() => {}), addEventListener() {}, pushErrorScope() {}, async popErrorScope() { return null; },
    createBuffer(d) { return { size: d.size, data: new ArrayBuffer(d.size), getMappedRange() { return this.data; }, unmap() {}, async mapAsync() {}, destroy() {} }; },
    createTexture(d) { const t = { label: d.label, width: d.size[0], height: d.size[1], format: d.format, usage: d.usage,
      createView() { return { texture: this }; }, destroy() {} }; textures.push(t); return t; },
    queue: { submit() {}, async onSubmittedWorkDone() {} },
    createCommandEncoder() { return {
      beginRenderPass() { events.push('begin'); return { end() { events.push('end'); } }; },
      copyTextureToBuffer() {}, copyTextureToTexture(src, dst) { events.push(['copy', src.texture.label, dst.texture.label]); }, finish() { return {}; },
    }; },
  };
  const context = { configure() {}, unconfigure() {}, getCurrentTexture() { return { width: 16, height: 16 }; } };
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { gpu: {
    async requestAdapter() { return { features: new Set(), async requestDevice() { return device; } }; },
    getPreferredCanvasFormat() { return 'rgba8unorm'; },
  } } });
  for (const [k, v] of Object.entries({ GPUBufferUsage: { VERTEX: 1, COPY_DST: 2, MAP_READ: 4 },
    GPUTextureUsage: { RENDER_ATTACHMENT: 1, COPY_SRC: 2, COPY_DST: 4, TEXTURE_BINDING: 8 },
    GPUShaderStage: { VERTEX: 1, FRAGMENT: 2 }, GPUMapMode: { READ: 1 } }))
    Object.defineProperty(globalThis, k, { configurable: true, value: v });
  try {
    const renderer = await createVkRenderer2({ width: 16, height: 16, getContext: () => context });
    const source = '0000000000000001', destination = '0000000000000002';
    const pass = id => ({ w: 16, h: 16, attachments: [{ imageId: id, samples: 1, isDepth: 0, loadOp: 1, storeOp: 0 }], clearColor: [1, 0, 0, 1], ops: [] });
    await renderer.render({ frameNo: 1, width: 16, height: 16, shaders: new Map(), images: new Map(), views: new Map(),
      samplers: new Map(), buffers: new Map(), bufHashes: new Map(), passes: [pass(source),
        { w: 16, h: 16, attachments: [], ops: [{ op: 'imageCopy', srcImage: source, dstImage: destination,
          srcMip: 0, srcLayer: 0, dstMip: 0, dstLayer: 0, srcOrigin: [0, 0, 0], dstOrigin: [0, 0, 0], extent: [16, 16, 1] }] }, pass(destination)] });
    assert.deepEqual(events.slice(0, 5), ['begin', 'end', ['copy', 'img:' + source, 'img:' + destination], 'begin', 'end']);
    assert.ok(textures.find(t => t.label === 'img:' + destination).usage & GPUTextureUsage.COPY_DST);
  } finally {
    for (const [k, descriptor] of saved) { if (descriptor) Object.defineProperty(globalThis, k, descriptor); else delete globalThis[k]; }
  }
});
