import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

test('PE Vulkan resolver exposes final dispatch entries and ICD loader exports', () => {
  const dir=mkdtempSync(join(tmpdir(),'bw64-vkpe-resolve-'));
  try {
    execFileSync('python3',['tools/vkd3d/probe/gen_vk64pe.py'],{stdio:'pipe'});
    let source=readFileSync('/tmp/vk64pe.c','utf8');
    // The private syscall is the host boundary. Keep the generated resolver
    // and its full export table; calls return dispatch IDs in this native test.
    source=source.replace(/static inline uint64_t vk64_trap[\s\S]*?\n}\n/, 'static uint64_t captured[4]; static inline uint64_t vk64_trap(uint64_t fnId, uint64_t *args) { for(int i=0;i<4;i++)captured[i]=args[i]; return fnId; }\n');
    const declarations='#define __declspec(x)\n#include <assert.h>\n';
    const main=`\nint main(void) {
      assert(vkGetDeviceProcAddr(0,"vkWaitSemaphores") == (void*)vkWaitSemaphores);
      assert(vkGetDeviceProcAddr(0,"vkWaitForFences") == (void*)vkWaitForFences);
      assert(vkGetDeviceProcAddr(0,"vkUpdateDescriptorSets") == (void*)vkUpdateDescriptorSets);
      assert(vkGetInstanceProcAddr(0,"vk_icdGetPhysicalDeviceProcAddr") == (void*)vk_icdGetPhysicalDeviceProcAddr);
      assert(vkGetInstanceProcAddr(0,"vk_icdNegotiateLoaderICDInterfaceVersion") == (void*)vk_icdNegotiateLoaderICDInterfaceVersion);
      assert(vkWaitSemaphores(0,0,0)==711);
      vkCmdSetViewportWithCount(0x1234,1,0x700010200ULL);
      assert(captured[0]==0x1234 && captured[1]==0 && captured[2]==1 && captured[3]==0x700010200ULL);
      vkCmdSetViewportWithCountEXT(0x1234,2,0x700010200ULL);
      assert(captured[1]==0 && captured[2]==2 && captured[3]==0x700010200ULL);
      vkCmdSetScissorWithCount(0x1234,1,0x700010200ULL);
      assert(captured[1]==0 && captured[2]==1 && captured[3]==0x700010200ULL);
      vkCmdSetScissorWithCountEXT(0x1234,2,0x700010200ULL);
      assert(captured[1]==0 && captured[2]==2 && captured[3]==0x700010200ULL);
      return 0;
    }\n`;
    writeFileSync(join(dir,'check.c'), declarations+source+main);
    execFileSync('cc',['-O2',join(dir,'check.c'),'-o',join(dir,'check')],{stdio:'pipe'});
    // Convert failed C assertions to a nonzero exit without leaving core files.
    const program=join(dir,'check');
    const result=execFileSync('bash',['-c','ulimit -c 0; exec "$1"','vkpe-check',program],{stdio:'pipe'});
    assert.ok(result !== null);
  } finally { rmSync(dir,{recursive:true,force:true}); }
});
