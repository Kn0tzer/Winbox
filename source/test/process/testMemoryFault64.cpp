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

// K-M3: a REP string op that faults midway must deliver the fault with the
// architectural partial progress in the frame (RCX/RSI/RDI count only the
// completed elements, flags from the last completed comparison), and a
// restart from the frame must finish the operation exactly. On the base
// tree the handlers run to completion on placeholder values and delivery
// restores the entry checkpoint, so the frame shows zero progress.
void testMemoryFault64RepPartial() {
    // REP MOVSQ, destination (write side) faults on element 3 of 4.
    {
        FaultFixture f;
        const U8 code2[] = {0xf3, 0x48, 0xa5}; // rep movsq
        f.mem.memcpyToGuest(code, code2, sizeof(code2));
        for (U32 i = 0; i < 4; i++) f.mem.writeq(data + i * 8, 0xa0 + i);
        f.mem.mprotect(data + 4096, 4096, 0);
        f.cpu.reg[X64_RCX].setU64(4);
        f.cpu.reg[X64_RSI].setU64(data);
        f.cpu.reg[X64_RDI].setU64(data + 4096 - 16);
        f.cpu.runBounded(1);
        if (!f.delivered() || f.saved(16) != code || f.saved(14) != 2 ||
            f.saved(9) != data + 16 || f.saved(8) != data + 4096)
            testFail("REP MOVSQ write fault lost partial progress (rcx/rsi/rdi/rip)");
        if (f.mem.readq(data + 4096 - 16) != 0xa0 || f.mem.readq(data + 4096 - 8) != 0xa1)
            testFail("REP MOVSQ completed elements were not copied before the fault");
        // Restart from the frame after fixing protection: finishes exactly.
        f.mem.mprotect(data + 4096, 4096, 3);
        f.cpu.reg[X64_RCX].setU64(f.saved(14));
        f.cpu.reg[X64_RSI].setU64(f.saved(9));
        f.cpu.reg[X64_RDI].setU64(f.saved(8));
        f.cpu.rip = f.saved(16);
        f.cpu.yield = false;
        f.cpu.runBounded(1);
        if (f.cpu.reg[X64_RCX].u64 != 0 || f.cpu.reg[X64_RSI].u64 != data + 32 ||
            f.cpu.reg[X64_RDI].u64 != data + 4096 + 16 ||
            f.mem.readq(data + 4096) != 0xa2 || f.mem.readq(data + 4096 + 8) != 0xa3)
            testFail("REP MOVSQ restart from the fault frame did not finish the copy");
    }
    // REP MOVSQ, source (read side) faults on element 3 of 4.
    {
        FaultFixture f;
        const U8 code2[] = {0xf3, 0x48, 0xa5}; // rep movsq
        f.mem.memcpyToGuest(code, code2, sizeof(code2));
        f.mem.writeq(data + 4096 - 16, 0xb0);
        f.mem.writeq(data + 4096 - 8, 0xb1);
        f.mem.mprotect(data + 4096, 4096, 0);
        f.cpu.reg[X64_RCX].setU64(4);
        f.cpu.reg[X64_RSI].setU64(data + 4096 - 16);
        f.cpu.reg[X64_RDI].setU64(stack + 1024);
        f.cpu.runBounded(1);
        if (!f.delivered() || f.saved(16) != code || f.saved(14) != 2 ||
            f.saved(9) != data + 4096 || f.saved(8) != stack + 1024 + 16)
            testFail("REP MOVSQ read fault lost partial progress (rcx/rsi/rdi/rip)");
        if (f.mem.readq(stack + 1024) != 0xb0 || f.mem.readq(stack + 1024 + 8) != 0xb1)
            testFail("REP MOVSQ read-side completed elements were not copied");
    }
    // REP STOSQ faults on element 3 of 4.
    {
        FaultFixture f;
        const U8 code2[] = {0xf3, 0x48, 0xab}; // rep stosq
        f.mem.memcpyToGuest(code, code2, sizeof(code2));
        f.mem.mprotect(data + 4096, 4096, 0);
        f.cpu.reg[X64_RAX].setU64(0xc5c5c5c5c5c5c5c5ULL);
        f.cpu.reg[X64_RCX].setU64(4);
        f.cpu.reg[X64_RDI].setU64(data + 4096 - 16);
        f.cpu.runBounded(1);
        if (!f.delivered() || f.saved(16) != code || f.saved(14) != 2 ||
            f.saved(8) != data + 4096)
            testFail("REP STOSQ fault lost partial progress (rcx/rdi/rip)");
        if (f.mem.readq(data + 4096 - 16) != 0xc5c5c5c5c5c5c5c5ULL ||
            f.mem.readq(data + 4096 - 8) != 0xc5c5c5c5c5c5c5c5ULL)
            testFail("REP STOSQ completed elements were not stored");
    }
    // REPNE SCASB faults on element 7 of 8 (scanning for 0x42 over 0x41
    // bytes, so the first six comparisons are unequal and REPNE keeps
    // going): flags must be the last completed comparison (unequal ->
    // ZF=0), not flags computed from the faulting element placeholder.
    {
        FaultFixture f;
        const U8 code2[] = {0xf2, 0xae}; // repne scasb
        f.mem.memcpyToGuest(code, code2, sizeof(code2));
        for (U32 i = 0; i < 6; i++) f.mem.writeb(data + 4090 + i, 0x41);
        f.mem.mprotect(data + 4096, 4096, 0);
        f.cpu.reg[X64_RAX].setU64(0x42);
        f.cpu.reg[X64_RCX].setU64(8);
        f.cpu.reg[X64_RDI].setU64(data + 4090);
        f.cpu.runBounded(1);
        if (!f.delivered() || f.saved(16) != code || f.saved(14) != 2 ||
            f.saved(8) != data + 4096 || (f.saved(17) & 0x40))
            testFail("REPNE SCASB fault lost partial progress or last-completed flags");
    }
    // Non-REP MOVSQ faulting on its only element: no progress, exact
    // pre-instruction frame (control: passes before and after K-M3).
    {
        FaultFixture f;
        const U8 code2[] = {0x48, 0xa5}; // movsq
        f.mem.memcpyToGuest(code, code2, sizeof(code2));
        f.mem.writeq(data, 0xd0);
        f.mem.mprotect(data + 4096, 4096, 0);
        f.cpu.reg[X64_RSI].setU64(data);
        f.cpu.reg[X64_RDI].setU64(data + 4096);
        f.cpu.runBounded(1);
        if (!f.delivered() || f.saved(16) != code ||
            f.saved(9) != data || f.saved(8) != data + 4096)
            testFail("non-REP MOVSQ fault changed the pre-instruction frame");
    }
}
#endif
