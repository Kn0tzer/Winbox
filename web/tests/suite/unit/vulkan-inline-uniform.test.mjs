import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('inline uniform updates own bytes and preserve partial updates after caller storage is reused',()=>{
 const source=readFileSync(join(root,'source/vulkan/vk64bridge.cpp'),'utf8');
 const binding=source.match(/struct Binding \{[\s\S]*?\};/)?.[0];
 const helper=source.match(/static void updateInlineUniform\([\s\S]*?\n\}/)?.[0];
 assert.ok(helper,'inline bytes must be copied during descriptor updates');
 assert.equal((source.match(/updateInlineUniform\(b,/g)||[]).length,1,'every update/template/push path copies inline bytes in applyDescWrite');
 assert.ok(/r\.inlData = at; r\.inlSize = te\.count;/.test(source),'inline template count measures bytes, not descriptors');
 const emitter=extractSourceRange(source, source.indexOf('} else if (b.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)'),source.indexOf('U64 activePipe = 0'));
 assert.ok(emitter.includes('p.bytes(b.inlineBytes)'));assert.ok(!emitter.includes('readGuestBytes'));
 const dir=mkdtempSync(join(tmpdir(),'vk-inline-'));
 try{
  writeFileSync(join(dir,'check.cpp'),`#include <cstdint>\n#include <vector>\n#include <cassert>\n#include <cstring>\nusing U8=uint8_t;using U32=uint32_t;using U64=uint64_t;\nstd::vector<U8> guest(8192);\nvoid readGuestBytes(void* dst,U64 addr,U64 len){assert(addr>=64 && addr+len<=guest.size());memcpy(dst,guest.data()+addr,len);}\n${binding}\n${helper}\nint main(){Binding b;b.obj=0x58;b.range=4096;for(unsigned i=0;i<4096;i++)guest[64+i]=i;updateInlineUniform(b,0,64,4096);assert(b.obj==0 && b.range==4096 && b.count==4096 && b.dstArrayElement==0);auto snapshot=b;std::fill(guest.begin(),guest.end(),0);for(unsigned i=0;i<4096;i++)assert(b.inlineBytes[i]==(U8)i);guest[5000]=42;guest[5001]=43;guest[5002]=44;guest[5003]=45;updateInlineUniform(b,4,5000,4);assert(b.inlineBytes[4]==42 && b.inlineBytes[7]==45 && b.inlineBytes[8]==8 && snapshot.inlineBytes[4]==4);updateInlineUniform(b,4100,5000,4);assert(b.inlineBytes.size()==4104 && b.inlineBytes[4099]==0 && b.inlineBytes[4100]==42);auto before=b.inlineBytes;updateInlineUniform(b,0,0,4);updateInlineUniform(b,0xfffffffc,64,8);assert(b.inlineBytes==before);}\n`);
  execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
