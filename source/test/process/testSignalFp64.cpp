#include "boxedwine.h"

#if defined(__TEST) && defined(BOXEDWINE_GUEST_X64)
#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "ksignal.h"
#include "../cpu/testCPU.h"

namespace {
constexpr U64 stack = 0x78100000;
constexpr U64 handler = 0x78200000;
constexpr U64 restorer = handler + 0x100;
constexpr U64 originalRip = handler + 0x200;
struct SignalFixture {
    KMemory64 memory{nullptr};
    CPU64 cpu{&memory};
    SignalFixture() {
        memory.mmapAnonymousFixed(stack, 16 * K64_PAGE_SIZE, 3);
        memory.mmapAnonymousFixed(handler, K64_PAGE_SIZE, 7);
        const U8 ret[] = {0xc3};
        const U8 sigreturn[] = {0xb8, 15, 0, 0, 0, 0x0f, 0x05};
        memory.memcpyToGuest(handler, ret, sizeof(ret));
        memory.memcpyToGuest(restorer, sigreturn, sizeof(sigreturn));
        cpu.reg[X64_RSP].setU64(stack + 16 * K64_PAGE_SIZE - 16);
        cpu.rip = originalRip;
        for (U32 sig : {K_SIGSEGV, K_SIGUSR1, K_SIGUSR2}) {
            auto& sa = cpu.sigActions[sig];
            sa.installed = true;
            sa.handler = handler;
            sa.restorer = restorer;
            sa.flags = 4; // SA_SIGINFO
        }
    }
    bool deliver(U32 sig = K_SIGUSR1) {
        return cpu.raiseSyncFault(sig, 14, 2, 0xdead0000, 5);
    }
    void sigreturn(U64 uctx) {
        cpu.reg[X64_RSP].setU64(uctx);
        cpu.reg[X64_RAX].setU64(15);
        ksyscall64(&cpu);
    }
};
}

void testSignalFp64Roundtrip() {
    // Assert the public ucontext/FXSAVE ABI using known raw values. The test
    // never uses the emulator's save/restore helpers as its oracle.
    for (bool edit : {false, true}) {
        SignalFixture f;
        CPU64& cpu = f.cpu;
        cpu.fpu.SetCW(0x077f);
        cpu.fpu.SetSW(3 << 11); // nonzero TOP distinguishes logical/physical ST
        for (U32 i = 0; i < 8; ++i) {
            cpu.fpu.tags[cpu.fpu.STV(i)] = i == 5 ? TAG_Empty : TAG_Valid;
            cpu.fpu.LD80(cpu.fpu.STV(i), 0x8000000000000000ULL + i, 0x3fff + i);
        }
        cpu.mxcsr = 0x3f80;
        for (U32 i = 0; i < 16; ++i) cpu.xmm[i] = {0xabc00000ULL + i, 0xdef00000ULL + i};
        cpu.reg[X64_RAX].setU64(0x12345678);
        cpu.rflags |= X64_DF;
        const U64 savedRsp = cpu.reg[X64_RSP].u64;
        if (!f.deliver()) { testFail("FP signal delivery failed"); return; }
        if ((cpu.reg[X64_RSP].u64 & 15) != 8) testFail("signal handler RSP violates SysV alignment");
        if (cpu.rflags & X64_DF) testFail("signal handler entered with direction flag set");
        const U64 info = cpu.reg[X64_RSI].u64;
        if (info <= cpu.reg[X64_RSP].u64 || f.memory.readd(info) != K_SIGUSR1)
            testFail("siginfo is below the active handler stack or malformed");
        const U64 uctx = cpu.reg[X64_RDX].u64;
        U64 fp = f.memory.readq(uctx + 224);
        if (!fp) { testFail("signal ucontext omits FP snapshot (K-M4)"); continue; }
        if (fp & 15) testFail("signal FP image is not FXSAVE aligned");
        if (f.memory.readw(fp) != 0x077f || f.memory.readw(fp + 2) != (3 << 11) ||
            f.memory.readd(fp + 24) != 0x3f80 ||
            f.memory.readq(fp + 32 + 7 * 16) != 0x8000000000000007ULL ||
            f.memory.readq(fp + 160 + 15 * 16 + 8) != 0xdef0000fULL)
            testFail("signal FP image has incorrect x87/SSE layout");
        if (edit) {
            // Linux restores the POINTER the handler supplies, not a fixed
            // inline slot. Move the image and alter high XMM, x87 and MXCSR.
            const U64 relocated = stack + 0x100;
            U8 image[512];
            f.memory.memcpyFromGuest(image, fp, sizeof(image));
            f.memory.memcpyToGuest(relocated, image, sizeof(image));
            fp = relocated;
            f.memory.writeq(uctx + 224, fp);
            f.memory.writeq(fp + 160 + 13 * 16, 0x123456789abcdef0ULL);
            f.memory.writeq(fp + 32 + 16, 0xf000000000000001ULL);
            f.memory.writed(fp + 24, 0x5f80);
        }
        cpu.mxcsr = 0x1f80;
        cpu.fpu.FINIT();
        for (auto& x : cpu.xmm) x = {};
        // Execute the actual guest RET and restorer syscall, so frame placement
        // must agree with the return ABI as well as the handler-visible fields.
        cpu.runBounded(3);
        if (cpu.rip != originalRip || cpu.reg[X64_RSP].u64 != savedRsp ||
            cpu.reg[X64_RAX].u64 != 0x12345678 || !(cpu.rflags & X64_DF))
            testFail("sigreturn lost interrupted GPR/flags state");
        if (cpu.mxcsr != (edit ? 0x5f80u : 0x3f80u) || cpu.fpu.cw != 0x077f || cpu.fpu.top != 3)
            testFail("sigreturn lost x87 control/TOP or MXCSR");
        for (U32 i = 0; i < 16; ++i) {
            if (cpu.xmm[i].lo != (edit && i == 13 ? 0x123456789abcdef0ULL : 0xabc00000ULL + i) ||
                cpu.xmm[i].hi != 0xdef00000ULL + i) testFail("sigreturn lost XMM%u", i);
        }
        for (U32 i = 0; i < 8; ++i) {
            U64 lo, hi;
            cpu.fpu.ST80(cpu.fpu.STV(i), &lo, &hi);
            if (lo != (edit && i == 1 ? 0xf000000000000001ULL : 0x8000000000000000ULL + i) ||
                hi != 0x3fff + i || (cpu.fpu.tags[cpu.fpu.STV(i)] == TAG_Empty) != (i == 5))
                testFail("sigreturn lost x87 ST%u or physical tag", i);
        }
    }
}

void testSignal64NestedAltstack() {
    SignalFixture f;
    auto& cpu = f.cpu;
    cpu.sigAltStack.ssSp = stack;
    cpu.sigAltStack.ssSize = 8 * K64_PAGE_SIZE;
    cpu.sigAltStack.ssFlags = 0;
    cpu.sigActions[K_SIGUSR1].flags |= 0x08000000;
    cpu.sigActions[K_SIGUSR2].flags |= 0x08000000;
    if (!f.deliver()) { testFail("outer altstack delivery failed"); return; }
    const U64 outerRsp = cpu.reg[X64_RSP].u64;
    const U64 outerUctx = cpu.reg[X64_RDX].u64;
    const U64 outerRip = f.memory.readq(outerUctx + 40 + 16 * 8);
    cpu.reg[X64_RSP].u64 -= 256; // handler locals/red zone must survive
    cpu.rip = handler + 0x80;
    const U64 innerSavedRsp = cpu.reg[X64_RSP].u64;
    if (!f.deliver(K_SIGUSR2)) { testFail("nested altstack delivery failed"); return; }
    const U64 innerUctx = cpu.reg[X64_RDX].u64;
    const U64 innerFp = f.memory.readq(innerUctx + 224);
    if (cpu.reg[X64_RSP].u64 >= innerSavedRsp || (innerFp && innerFp + 512 > innerSavedRsp - 128))
        testFail("nested altstack frame overwrote active handler stack");
    if (f.memory.readq(outerUctx + 40 + 16 * 8) != outerRip)
        testFail("nested altstack delivery overwrote outer saved RIP");
    f.sigreturn(innerUctx);
    if (cpu.rip != handler + 0x80 || cpu.reg[X64_RSP].u64 != innerSavedRsp)
        testFail("nested sigreturn did not resume outer handler");
    cpu.reg[X64_RSP].setU64(outerRsp);
    f.sigreturn(outerUctx);
    if (cpu.rip != originalRip) testFail("outer sigreturn lost original RIP");
}

void testSignal64BadFpFrame() {
    for (bool badPointer : {false, true}) {
        SignalFixture f;
        if (!f.deliver()) { testFail("bad-FP fixture delivery failed"); return; }
        const U64 uctx = f.cpu.reg[X64_RDX].u64;
        const U64 fp = f.memory.readq(uctx + 224);
        if (!fp) { testFail("bad-FP test cannot find signal FP image"); continue; }
        if (badPointer) f.memory.writeq(uctx + 224, 0x7fff000000000000ULL);
        else f.memory.writed(fp + 24, 0xffffffff);
        f.sigreturn(uctx);
        if (f.cpu.rip == originalRip) testFail("invalid signal FP frame resumed interrupted code");
    }
}
void testSignal64AsyncFp() {
    SignalFixture f;
    TestContext& context = testContext();
    auto& cpu = f.cpu;
    cpu.thread = context.thread;
    cpu.xmm[11] = {0x1111222233334444ULL, 0x5555666677778888ULL};
    const U64 bit = 1ULL << (K_SIGUSR1 - 1);
    U64 oldPending;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(context.thread->pendingSignalsMutex);
        oldPending = context.thread->pendingSignals;
        context.thread->pendingSignals = bit;
    }
    const bool delivered = cpu.deliverPendingSignals();
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(context.thread->pendingSignalsMutex);
        if (context.thread->pendingSignals & bit) testFail("async signal pending bit was not consumed");
        context.thread->pendingSignals = oldPending;
    }
    if (!delivered) { testFail("async signal was not delivered"); return; }
    const U64 uctx = cpu.reg[X64_RDX].u64;
    const U64 fp = f.memory.readq(uctx + 224);
    if (!fp || f.memory.readq(fp + 160 + 11 * 16) != 0x1111222233334444ULL)
        testFail("async delivery omitted interrupted FP state");
    if (cpu.xmm[11].lo || cpu.xmm[11].hi || cpu.mxcsr != 0x1f80)
        testFail("async handler did not receive initialized FP state");
    // A null FP pointer has specified initialization semantics on Linux.
    f.memory.writeq(uctx + 224, 0);
    cpu.xmm[11] = {1, 2};
    cpu.mxcsr = 0x5f80;
    f.sigreturn(uctx);
    if (cpu.rip != originalRip || cpu.xmm[11].lo || cpu.xmm[11].hi ||
        cpu.mxcsr != 0x1f80 || cpu.fpu.cw != 0x037f)
        testFail("null signal FP pointer did not initialize FP state");
}
#endif
