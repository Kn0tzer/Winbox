import { extractSourceRange } from '../lib/source-slice.mjs';
import {test} from 'node:test';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {execFileSync} from 'node:child_process';
test('Vulkan image allocation uses component widths for 16-bit RGBA and RG floats',()=>{
 const repo=resolve(import.meta.dirname,'../../../..'),source=readFileSync(join(repo,'source/vulkan/vk64bridge.cpp'),'utf8'),header=readFileSync(join(repo,'source/vulkan/vk64_guest.h'),'utf8');
 const enums=[...header.matchAll(/(VK_FORMAT_\w+)\s*=\s*(\d+)/g)].map(m=>`${m[1]}=${m[2]}`).join(',');
 const code=extractSourceRange(source, source.indexOf('U32 fmt_bpp('),source.indexOf('const char* fmt_name('));
 const dir=mkdtempSync(join(tmpdir(),'bw-image-bpp-'));try {
  const c=join(dir,'size.cpp'),bin=join(dir,'size');writeFileSync(c,`#include <cassert>\n#include <algorithm>\n#include <cstdint>\nusing U32=uint32_t;using U64=uint64_t;enum {${enums}};\nstruct Image {U32 w,h,d,layers,mips,format;};\n${code}\nint main(){assert(fmt_bpp(VK_FORMAT_R16G16B16A16_UNORM)==8);assert(fmt_bpp(VK_FORMAT_R16G16_SFLOAT)==4);Image lut{1024,512,1,1,1,VK_FORMAT_R16G16B16A16_UNORM};assert(imageDataBytes(lut)==4194304);Image cube{2,2,1,6,2,VK_FORMAT_R16G16B16A16_SFLOAT};assert(imageLayerBytes(cube)==40);assert(imageSubresourceOffset(cube,1,5)==232);}`);
  execFileSync('c++',['-std=c++17',c,'-o',bin],{stdio:'pipe'});execFileSync(bin,[],{stdio:'pipe'});
 }finally {rmSync(dir,{recursive:true,force:true});}
});
