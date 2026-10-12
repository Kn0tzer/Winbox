import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('64-bit query reset resolves to its typed trap and preserves full-width handles',()=>{
 let source=readFileSync(join(root,'tools/rootfs64/libvk64/libvk64.c'),'utf8');
 source=source.replace(/static inline uint64_t vk64_trap\(uint64_t fnId, VK64Args\* args\) \{[\s\S]*?\n\}/,`static uint64_t seen;static VK64Args captured;
static inline uint64_t vk64_trap(uint64_t fnId,VK64Args* args){seen=fnId;if(args)captured=*args;return 0;}`);
 const dir=mkdtempSync(join(tmpdir(),'vk64-query-reset-'));
 try{
  writeFileSync(join(dir,'check.c'),source+`
int main(void){typedef void(*Reset)(uint64_t,uint64_t,uint32_t,uint32_t);Reset f=(Reset)vkGetDeviceProcAddr(1,"vkResetQueryPool");if(!f)return 1;
f(0x100000008ULL,0x200000009ULL,7,11);return !(seen==851 && captured.a[0]==0x100000008ULL && captured.a[1]==0x200000009ULL && captured.a[2]==7 && captured.a[3]==11);}
`);
  execFileSync('cc',['-O0','-fno-builtin',join(dir,'check.c'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
