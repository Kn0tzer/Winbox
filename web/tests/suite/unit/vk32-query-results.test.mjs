import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('i386 query results use 32-bit size_t without shifting the data pointer, stride or flags',()=>{
 const src=readFileSync(join(root,'tools/rootfs64/libvk32/libvk32.c'),'utf8');
 const rewritten=src.replace(/static inline uint64_t vk64_trap\(uint64_t fnId, VK64Args\* args\) \{[\s\S]*?\n\}/,`static uint64_t capturedFn; static VK64Args capturedArgs;
static inline uint64_t vk64_trap(uint64_t fnId, VK64Args* args) { capturedFn=fnId;capturedArgs=*args;return 0; }`);
 assert.notEqual(src,rewritten);
 const dir=mkdtempSync(join(tmpdir(),'vk32-query-'));
 try{
  writeFileSync(join(dir,'check.c'),rewritten+`
static unsigned char bytes[88];
static int check(void){
 typedef VkResult (*QueryFn)(VkDevice,VkQueryPool,uint32_t,uint32_t,unsigned long,void*,uint64_t,VkFlags);
 QueryFn f=(QueryFn)vkGetDeviceProcAddr(1,"vkGetQueryPoolResults");
 if(!f)return 10;
 if(f(0x324b0001,0x324b0002ULL,3,1,sizeof(bytes),bytes,88,1)!=0)return 11;
 if(capturedFn!=VK64_fn_vkGetQueryPoolResults)return 12;
 if(capturedArgs.a[0]!=0x324b0001 || capturedArgs.a[1]!=0x324b0002ULL || capturedArgs.a[2]!=3 || capturedArgs.a[3]!=1)return 13;
 if(capturedArgs.a[4]!=88)return 14;
 if(capturedArgs.a[5]!=P(bytes))return 15;
 if(capturedArgs.a[6]!=88 || capturedArgs.a[7]!=1)return 16;
 f(1,2,0,1,sizeof(bytes),bytes,0x100000058ULL,5);
 if(capturedArgs.a[4]!=88 || capturedArgs.a[5]!=P(bytes) || capturedArgs.a[6]!=0x100000058ULL || capturedArgs.a[7]!=5)return 17;
 return 0;
}
void _start(void){int result=check();__asm__ volatile("int $0x80"::"a"(1),"b"(result):"memory");__builtin_unreachable();}
`);
  const c=join(dir,'check.c'),bin=join(dir,'check');
  execFileSync('i686-linux-gnu-gcc',['-std=c11','-O0','-Werror','-nostdlib','-static','-fno-pie','-fno-stack-protector','-Wl,-e,_start',c,'-o',bin]);
  execFileSync('qemu-i386',[bin],{timeout:10000});
 }finally{rmSync(dir,{recursive:true,force:true});}
});
