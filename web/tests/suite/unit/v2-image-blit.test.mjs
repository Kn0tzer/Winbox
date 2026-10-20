import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { decodeV2Frame, V2_REC, v2ImageBlitRegion, v2TextureMipUploadPlan } from '../../../vkwebgpu.mjs';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

function bytesForBlit({ src = 1n, dst = 2n, srcMip = 1, srcLayer = 2, dstMip = 3, dstLayer = 4, filter = 1,
  srcOffsets = [[0, 1, 0], [8, 7, 1]], dstOffsets = [[1, 2, 0], [6, 5, 1]] } = {}) {
  const bytes = new Uint8Array(84), view = new DataView(bytes.buffer);
  view.setBigUint64(0, src, true); view.setBigUint64(8, dst, true);
  [srcMip, srcLayer, dstMip, dstLayer, filter].forEach((value, i) => view.setUint32(16 + i * 4, value, true));
  [...srcOffsets.flat(), ...dstOffsets.flat()].forEach((value, i) => view.setInt32(36 + i * 4, value, true));
  return bytes;
}
const frameRecords = payload => [
  {type:V2_REC.FRAME_BEGIN,payload:new Uint8Array(new Uint32Array([1,16,16,0]).buffer)},
  {type:V2_REC.IMAGE_BLIT,payload},
  {type:V2_REC.FRAME_END,payload:new Uint8Array(new Uint32Array([1]).buffer)},
];

test('mip upload plan covers every mip and array layer in bridge order', () => {
  const plan = v2TextureMipUploadPlan({w:8,h:4,d:1,layers:2,mips:3}, 'rgba8unorm', '2d-array');
  assert.deepEqual(plan.map(({layer,mip,offset,byteLength,extent}) => ({layer,mip,offset,byteLength,extent})), [
    {layer:0,mip:0,offset:0,byteLength:128,extent:[8,4,1]},
    {layer:0,mip:1,offset:128,byteLength:32,extent:[4,2,1]},
    {layer:0,mip:2,offset:160,byteLength:8,extent:[2,1,1]},
    {layer:1,mip:0,offset:168,byteLength:128,extent:[8,4,1]},
    {layer:1,mip:1,offset:296,byteLength:32,extent:[4,2,1]},
    {layer:1,mip:2,offset:328,byteLength:8,extent:[2,1,1]},
  ]);
  assert.deepEqual(v2TextureMipUploadPlan({w:8,h:4,d:4,layers:1,mips:3}, 'rgba8unorm', '3d').map(x => [x.offset,x.byteLength,x.extent]),
    [[0,512,[8,4,4]],[512,64,[4,2,2]],[576,8,[2,1,1]]]);
});

test('IMAGE_BLIT signed offsets and filter decode and form a scaled region', () => {
  const frame = decodeV2Frame(frameRecords(bytesForBlit()));
  const op = frame.passes[0].ops[0];
  assert.deepEqual(op, {op:'imageBlit',srcImage:'0000000000000001',dstImage:'0000000000000002',srcMip:1,srcLayer:2,dstMip:3,dstLayer:4,filter:1,
    srcOffsets:[[0,1,0],[8,7,1]],dstOffsets:[[1,2,0],[6,5,1]]});
  assert.deepEqual(v2ImageBlitRegion({...op,srcMip:0,dstMip:0},{w:16,h:16,d:1},{w:8,h:8,d:1}), {
    viewport:[1,2,5,3],sourceRect:[0,1/16,0.5,6/16],srcW:16,srcH:16,dstW:8,dstH:8,
  });
  assert.throws(() => decodeV2Frame(frameRecords(bytesForBlit({filter:2}))), /invalid image blit/);
});

test('native 64-bit and 32-bit blit paths record image subresource, filter and offsets', () => {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const structure = bridge.match(/struct ImageBlit \{[\s\S]*?\n\};/)?.[0];
  const method = bridge.match(/void recordImageBlit\([\s\S]*?\n\}/)?.[0];
  assert.ok(structure && method);
  assert.ok((bridge.match(/recordImageBlit\(/g) || []).length >= 4, 'both ABI handlers call the shared recorder');
  assert.match(bridge, /emit\(V2_IMAGE_BLIT,[\s\S]*?blit\.filter/);
  const directory = mkdtempSync('/tmp/winbox-finish/work-G1/image-blit-');
  try {
    const source = join(directory, 'check.cpp'), binary = join(directory, 'check');
    writeFileSync(source, `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
using U32=uint32_t;using U64=uint64_t;const U32 VK64_MAX_CMDS=16;enum{CMD_BLIT_IMAGE};
${structure}
struct Cmd{U32 kind=0;ImageBlit imageBlit;};struct CmdBuf{std::vector<Cmd>cmds;};
void pushCmd(CmdBuf*cb,const Cmd&c){cb->cmds.push_back(c);}
${method}
int main(){CmdBuf cb;int32_t s[2][3]={{1,2,0},{8,9,1}},d[2][3]={{0,0,0},{4,4,1}};recordImageBlit(&cb,11,12,3,4,5,6,1,s,d,2);assert(cb.cmds.size()==2);auto&a=cb.cmds[0].imageBlit;auto&b=cb.cmds[1].imageBlit;assert(a.src==11&&a.dst==12&&a.srcMip==3&&a.srcLayer==4&&a.dstMip==5&&a.dstLayer==6&&a.filter==1);assert(a.srcOffsets[1][1]==9&&b.srcLayer==5&&b.dstLayer==7);}
`);
    execFileSync('c++',['-std=c++17','-Wall','-Wextra','-Werror',source,'-o',binary]);
    execFileSync(binary);
  } finally { rmSync(directory,{recursive:true,force:true}); }
});

test('GPU blit draws a fullscreen triangle into the requested destination mip and layer', async () => {
  const gpu = createValidationDevice();
  try {
    const renderer = await createVkRenderer2(gpu.canvas,{translate:async()=> 'fn main() {}'});
    const image = () => ({w:8,h:8,d:1,layers:2,mips:3,format:37,hash:'x',pixels:new Uint8Array(8*8*4*2 + 4*4*4*2 + 2*2*4*2)});
    const source='0000000000000001',destination='0000000000000002';
    const op={op:'imageBlit',srcImage:source,dstImage:destination,srcMip:0,srcLayer:1,dstMip:1,dstLayer:0,filter:0,
      srcOffsets:[[0,0,0],[8,8,1]],dstOffsets:[[0,0,0],[4,4,1]]};
    const result=await renderer.render({frameNo:1,width:8,height:8,shaders:new Map(),images:new Map([[source,image()],[destination,image()]]),views:new Map(),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),passes:[{w:8,h:8,attachments:[],ops:[op]}]},{present:false});
    assert.equal(result.rendered,true);
    assert.equal(renderer.stats.unsupported,0,JSON.stringify(renderer.stats));
    const view=gpu.passes[0].colorAttachments[0].view;
    assert.equal(view.options.baseMipLevel,1);assert.equal(view.options.baseArrayLayer,0);
    assert.deepEqual(gpu.viewports.at(-1),[0,0,4,4,0,1]);assert.equal(gpu.draws.length,1);
  } finally { gpu.restore(); }
});
