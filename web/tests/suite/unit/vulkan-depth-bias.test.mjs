import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';
import { v2DepthBiasState, v2PrimitiveState } from '../../../vkwebgpu.mjs';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
class W {
  constructor() { this.bytes = []; }
  u32(n) { for (let i=0;i<4;i++) this.bytes.push((n >>> (8*i)) & 255); return this; }
  u64(n) { this.u32(n); this.u32(0); return this; }
  f32(n) { const b = new Uint8Array(new Float32Array([n]).buffer); this.bytes.push(...b); return this; }
  buf() { return new Uint8Array(this.bytes); }
}
function pipeline() {
  const w = new W().u64(1).u64(2).u64(3);
  [3,0,0,1,1,3,0,0,0].forEach(n => w.u32(n));
  w.u32(0).u32(0); // vertex and fragment specialization lists
  w.u32(0x31444e42).u32(0); [0,0,0,0].forEach(n => w.f32(n));
  w.u32(0x31534244).u32(1).u32(1).u32(1).f32(-1.4).f32(0.75).f32(2.25);
  return w.buf();
}
function frameRecords() {
  const begin = new W().u32(1).u32(4).u32(4).u32(0).buf();
  const rp = new W().u64(1).u64(2).u32(4).u32(4).u32(0)
    .f32(0).f32(0).f32(0).f32(1).f32(1).u32(0).buf();
  const bias = new W().f32(0.25).f32(-0.5).f32(1.75).buf();
  const end = new W().u32(1).buf();
  return [
    {type:V2_REC.FRAME_BEGIN,payload:begin}, {type:V2_REC.RP_BEGIN,payload:rp},
    {type:V2_REC.PIPELINE,payload:pipeline()}, {type:0x18,payload:bias},
    {type:V2_REC.RP_END,payload:new Uint8Array(0)}, {type:V2_REC.FRAME_END,payload:end},
  ];
}

test('PIPELINE and dynamic depth-bias records decode in recorded order', () => {
  assert.equal(V2_REC.DEPTH_BIAS, 0x18);
  const frame = decodeV2Frame(frameRecords());
  const pipe = frame.pipelines.get('0000000000000001');
  assert.deepEqual({
    depthBiasEnable:pipe.depthBiasEnable, depthClampEnable:pipe.depthClampEnable,
    depthBiasDynamic:pipe.depthBiasDynamic, depthBiasConstantFactor:pipe.depthBiasConstantFactor,
    depthBiasClamp:pipe.depthBiasClamp, depthBiasSlopeFactor:pipe.depthBiasSlopeFactor,
  }, {depthBiasEnable:1,depthClampEnable:1,depthBiasDynamic:true,depthBiasConstantFactor:Math.fround(-1.4),depthBiasClamp:0.75,depthBiasSlopeFactor:2.25});
  assert.deepEqual(frame.passes[0].ops.filter(op => op.op === 'depthBias'), [
    {op:'depthBias',constantFactor:0.25,clamp:-0.5,slopeFactor:1.75},
  ]);
});

test('pipeline depth bias rounds constantFactor and enables unclipped depth only with its feature', () => {
  assert.deepEqual(v2DepthBiasState({depthBiasEnable:1,depthBiasConstantFactor:-1.6,depthBiasClamp:0.75,depthBiasSlopeFactor:2.25}), {
    depthBias:-2, depthBiasClamp:0.75, depthBiasSlopeScale:2.25,
  });
  const pipe = {topology:3,cull:0,front:0,depthClampEnable:1};
  assert.deepEqual(v2PrimitiveState(pipe,true), {topology:'triangle-list',stripIndexFormat:undefined,cullMode:'none',frontFace:'ccw',unclippedDepth:true});
  assert.deepEqual(v2PrimitiveState(pipe,false), {topology:'triangle-list',stripIndexFormat:undefined,cullMode:'none',frontFace:'ccw'});
});

test('native depth-bias tail emission round-trips through the V2 decoder', () => {
  const bridge = readFileSync(join(root,'source/vulkan/vk64bridge.cpp'),'utf8');
  const emitter = bridge.match(/static void emitDepthBias\([\s\S]*?\n\}/)[0];
  const dir = mkdtempSync(join('/tmp/winbox-finish/work-G1/','depth-bias-'));
  try {
    const source=join(dir,'depth-bias.cpp'), binary=join(dir,'depth-bias');
    writeFileSync(source,`
#include <cstdint>
#include <cstdio>
#include <vector>
using U32=uint32_t;
struct ChunkWriter { std::vector<unsigned char> bytes; void u32(U32 n){for(int i=0;i<4;i++)bytes.push_back((unsigned char)(n>>(8*i)));} void f32(float f){U32 n;__builtin_memcpy(&n,&f,4);u32(n);} };
${emitter}
int main(){ChunkWriter w;emitDepthBias(w,1,1,1,-1.4f,0.75f,2.25f);for(auto b:w.bytes)putchar(b);}
`);
    execFileSync('c++',['-std=c++17','-Wall','-Wextra','-Werror',source,'-o',binary]);
    const tail=new Uint8Array(execFileSync(binary));
    assert.equal(tail.byteLength,28);
    const payload=pipeline().subarray(0,pipeline().length-28);
    const records=frameRecords();
    records[2]={type:V2_REC.PIPELINE,payload:new Uint8Array([...payload,...tail])};
    const pipe=decodeV2Frame(records).pipelines.get('0000000000000001');
    assert.equal(pipe.depthBiasEnable,1);
    assert.equal(pipe.depthClampEnable,1);
    assert.equal(pipe.depthBiasConstantFactor,-1.399999976158142);
  } finally { rmSync(dir,{recursive:true,force:true}); }
});

test('32-bit and 64-bit guest shims forward vkCmdSetDepthBias float bits through append-only ID 905', () => {
  for (const arch of ['32','64']) {
    const path=join(root,`tools/rootfs64/libvk${arch}/libvk${arch}.c`);
    const original=readFileSync(path,'utf8');
    const source=original.replace(/static inline uint64_t vk64_trap\(uint64_t fnId, VK64Args\* args\) \{[\s\S]*?\n\}/,
      'static uint64_t capturedFn; static VK64Args capturedArgs; static inline uint64_t vk64_trap(uint64_t fnId, VK64Args* args) { capturedFn=fnId; if(args) capturedArgs=*args; return 0; }');
    assert.notEqual(source,original);
    const dir=mkdtempSync(join('/tmp/winbox-finish/work-G1/',`depth-bias-shim-${arch}-`));
    try {
      const check=`
int check_depth_bias(void){vkCmdSetDepthBias(1,1.25f,-0.5f,2.75f);return !(capturedFn==905 && capturedArgs.a[0]==1 && capturedArgs.a[1]==0x3fa00000 && capturedArgs.a[2]==0xbf000000 && capturedArgs.a[3]==0x40300000);}
`;
      const src=join(dir,'check.c'),bin=join(dir,'check');
      writeFileSync(src,source+check+(arch==='32'?`void _start(void){int x=check_depth_bias();__asm__ volatile("int $0x80"::"a"(1),"b"(x):"memory");__builtin_unreachable();}`:`int main(void){return check_depth_bias();}`));
      const cc=arch==='32'?'i686-linux-gnu-gcc':'cc';
      execFileSync(cc,arch==='32'?['-O0','-fno-builtin','-nostdlib','-static','-fno-pie','-fno-stack-protector','-Wl,-e,_start',src,'-o',bin]:['-O0','-fno-builtin',src,'-o',bin]);
      if(arch==='32') execFileSync('qemu-i386',[bin]); else execFileSync(bin);
    } finally { rmSync(dir,{recursive:true,force:true}); }
  }
});
