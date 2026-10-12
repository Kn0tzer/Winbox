import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
const transfers=[
 ['vkCmdCopyBuffer',812],['vkCmdCopyBuffer2',813],['vkCmdCopyBuffer2KHR',814],
 ['vkCmdCopyBufferToImage2',821],['vkCmdCopyBufferToImage2KHR',822],
];
test('32-bit Vulkan transfer commands resolve to real ABI dispatch, including Vulkan 1.3 uploads',()=>{
 const src=readFileSync(join(root,'tools/rootfs64/libvk32/libvk32.c'),'utf8');
 const rewritten=src.replace(/static inline uint64_t vk64_trap\(uint64_t fnId, VK64Args\* args\) \{[\s\S]*?\n\}/,`static uint64_t capturedFn; static VK64Args capturedArgs;
static inline uint64_t vk64_trap(uint64_t fnId, VK64Args* args) {
 capturedFn=fnId; if(args)capturedArgs=*args; return 0;
}`);
 assert.notEqual(src,rewritten,'trap boundary replaced by local test collector');
 const checks=transfers.map(([name,id],i)=>{
  const legacy=name==='vkCmdCopyBuffer';
  const signature=legacy?'VkCommandBuffer,VkBuffer,VkBuffer,uint32_t,const void*':'VkCommandBuffer,const void*';
  const call=legacy?'0x324b0001,0x324b0002ULL,0x324b0003ULL,1,info':'0x324b0001,info';
  const args=legacy?'capturedArgs.a[1]!=0x324b0002ULL || capturedArgs.a[2]!=0x324b0003ULL || capturedArgs.a[3]!=1 || capturedArgs.a[4]!=P(info)':'capturedArgs.a[1]!=P(info)';
  return `{ PFN_vkVoidFunction f=vkGetDeviceProcAddr(1,"${name}"); if(!f)return ${10+i}; ((void(*)(${signature}))f)(${call}); if(capturedFn!=${id} || capturedArgs.a[0]!=0x324b0001 || ${args}) return ${20+i}; }`;
 }).join('\n');
 const dir=mkdtempSync(join(tmpdir(),'vk32-transfer-'));
 try{
  writeFileSync(join(dir,'check.c'),rewritten+`\nint main(void){unsigned char info[128]={0};\n${checks}\nreturn 0;}\n`);
  execFileSync('cc',['-std=c11','-Werror',join(dir,'check.c'),'-o',join(dir,'check')]);
  execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
