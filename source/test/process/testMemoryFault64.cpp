#include "boxedwine.h"
#if defined(__TEST) && defined(BOXEDWINE_GUEST_X64)
#include "cpu64.h"
#include "kmemory64.h"
#include "jit64.h"
#include "ksignal.h"
#include "../cpu/testCPU.h"

namespace {
constexpr U64 code = 0x79100000, stack = 0x79200000, data = 0x79300000;
constexpr U64 sentinel = 0x123456789abcdef0ULL;
struct FaultFixture {
    KMemory64 mem{nullptr};
    CPU64 cpu{&mem};
    FaultFixture() {
        mem.mmapAnonymousFixed(code, 4096, 7);
        mem.mmapAnonymousFixed(stack, 16384, 3);
        mem.mmapAnonymousFixed(data, 8192, 3);
        mem.writeb(code + 0x800, 0xcc); // stop after taking the signal
        cpu.sigActions[K_SIGSEGV].installed = true;
        cpu.sigActions[K_SIGSEGV].handler = code + 0x800;
        cpu.reg[X64_RSP].setU64(stack + 16384 - 16);
        cpu.reg[X64_RAX].setU64(sentinel);
        cpu.reg[X64_RBX].setU64(data);
        cpu.rip = code;
    }
    U64 saved(U32 index) { return mem.readq(cpu.reg[X64_RDX].u64 + 40 + index * 8); }
    bool delivered() {
        const U64 info = cpu.reg[X64_RSI].u64;
        return info >= stack && info < stack + 16384 && mem.readd(info) == K_SIGSEGV;
    }
};
}

void testMemoryFault64Registers() {
    {
        FaultFixture f;
        const U8 load[] = {0x48, 0x8b, 0x03}; // mov rax,[rbx]
        f.mem.memcpyToGuest(code, load, sizeof(load));
        f.mem.writeq(data, sentinel + 1);
        f.mem.mprotect(data, 4096, 0);
        // Kernel/loader copies keep their deliberately unchecked access.
        if (f.mem.readq(data) != sentinel + 1) testFail("generic accessor semantics changed outside guest execution");
        f.cpu.runBounded(1);
        if (!f.delivered() || f.saved(16) != code || f.saved(13) != sentinel)
            testFail("faulting load corrupted saved RAX/RIP instead of preserving inputs");
    }
    {
        FaultFixture f;
        const U8 add[] = {0x48, 0x83, 0x03, 0x01}; // add qword [rbx],1
        f.mem.memcpyToGuest(code, add, sizeof(add));
        f.mem.writeq(data, ~0ULL);
        f.mem.mprotect(data, 4096, 1);
        const U32 flags = f.cpu.rflags;
        f.cpu.runBounded(1);
        if (!f.delivered() || f.saved(17) != flags || f.mem.readq(data) != ~0ULL)
            testFail("faulting read/modify/write changed saved flags or protected memory");
    }
    {
        FaultFixture f;
        const U8 push[] = {0x50}; // push rax: fault must not decrement saved RSP
        f.mem.memcpyToGuest(code, push, sizeof(push));
        f.mem.mprotect(data, 4096, 0);
        const U64 originalRsp = data + 8;
        f.cpu.reg[X64_RSP].setU64(originalRsp);
        f.cpu.sigAltStack.ssSp = stack;
        f.cpu.sigAltStack.ssSize = 16384;
        f.cpu.sigAltStack.ssFlags = 0;
        f.cpu.sigActions[K_SIGSEGV].flags = 0x08000004;
        f.cpu.runBounded(1);
        if (!f.delivered() || f.saved(15) != originalRsp)
            testFail("faulting stack push changed saved RSP before delivery");
    }
    {
        FaultFixture f;
        const U8 load[] = {0xf3, 0x0f, 0x6f, 0x03}; // movdqu xmm0,[rbx]
        f.mem.memcpyToGuest(code, load, sizeof(load));
        f.cpu.reg[X64_RBX].setU64(data + 4096 - 8);
        f.cpu.xmm[0] = {sentinel, sentinel + 1};
        f.mem.writeq(data + 4096 - 8, 0xdeadbeef);
        f.mem.mprotect(data + 4096, 4096, 0);
        f.cpu.runBounded(1);
        const U64 fp = f.mem.readq(f.cpu.reg[X64_RDX].u64 + 224);
        if (!f.delivered() || !fp || f.mem.readq(fp + 160) != sentinel || f.mem.readq(fp + 168) != sentinel + 1)
            testFail("cross-page SIMD load changed saved XMM before fault");
    }
}

void testMemoryFault64JitPoll() {
    // The same entry runs twice: first warm the decoded cache with RW data,
    // then revoke access without touching the code. BW64_JIT=1 must prove an
    // actual block executed, not merely repeat interpreter tests with an env.
    FaultFixture f;
    const U8 loop[] = {0x48,0x8b,0x03, 0x48,0x83,0xc0,0x01, 0x75,0xf7};
    f.mem.memcpyToGuest(code, loop, sizeof(loop));
    f.mem.writeq(data, 1);
    f.cpu.runBounded(9);
    if (jit64Enabled() && (!f.cpu.jit() || !f.cpu.jit()->blocksExecuted))
        testFail("JIT-mode test never entered the cached block engine");
    f.cpu.rip = code;
    f.cpu.yield = false;
    f.cpu.reg[X64_RAX].setU64(sentinel);
    f.mem.mprotect(data, 4096, 0);
    f.cpu.runBounded(9);
    if (!f.delivered() || f.saved(16) != code || f.saved(13) != sentinel)
        testFail("cached block ignored a safepoint poll-page fault or corrupted its inputs");
}
#endif
