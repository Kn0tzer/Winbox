// tools/cpu-diff/avx-probe.c — prints the AVX-related CPU features a guest sees,
// and whether one VEX-encoded instruction actually executes.
//
// Build (static x86-64, so it runs unchanged on the emulator or qemu-x86_64):
//   x86_64-linux-gnu-gcc -O2 -static -o avx-probe tools/cpu-diff/avx-probe.c
//   qemu-x86_64 ./avx-probe        # reference run: qemu's own CPU model
//
// The VEX line is the one that matters. An unimplemented VEX opcode can be
// skipped instead of faulting (see bw64RecoverEnabled in source/util/log.cpp),
// so the check reads the destination register. A SIGILL alone would not be
// enough: a skipped instruction raises nothing.
#include <cpuid.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static sigjmp_buf trap;
static void onIll(int sig) {
    (void)sig;
    siglongjmp(trap, 1);
}

int main(void) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    __cpuid(1, a, b, c, d);
    const unsigned osxsave = (c >> 27) & 1;
    printf("cpuid.1.ecx.osxsave=%u\n", osxsave);
    printf("cpuid.1.ecx.avx=%u\n", (c >> 28) & 1);

    __cpuid_count(7, 0, a, b, c, d);
    printf("cpuid.7.0.ebx.bmi1=%u\n", (b >> 3) & 1);
    printf("cpuid.7.0.ebx.avx2=%u\n", (b >> 5) & 1);
    printf("cpuid.7.0.ebx.bmi2=%u\n", (b >> 8) & 1);

    if (osxsave) {
        uint32_t lo = 0, hi = 0;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        printf("xgetbv0.eax=0x%x\n", lo);
    } else {
        // XGETBV is #UD when OSXSAVE is clear on real hardware, so do not run it.
        printf("xgetbv0.eax=n/a (osxsave clear)\n");
    }

    // VEX.128 vmovd %eax,%xmm1 (C5 F9 6E C8). xmm1 is zeroed first, so an
    // instruction that did not run leaves 0 behind.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onIll;
    sigaction(SIGILL, &sa, NULL);

    volatile int faulted = 0;
    uint32_t out = 0;
    if (sigsetjmp(trap, 1) == 0) {
        __asm__ volatile(
            "pxor %%xmm1, %%xmm1\n\t"
            "mov $0x5a5a5a5a, %%eax\n\t"
            ".byte 0xc5,0xf9,0x6e,0xc8\n\t"
            "movd %%xmm1, %0\n\t"
            : "=r"(out)
            :
            : "eax", "xmm1");
    } else {
        faulted = 1;
    }
    if (faulted) printf("vex.vmovd=SIGILL\n");
    else if (out == 0x5a5a5a5au) printf("vex.vmovd=executed\n");
    else printf("vex.vmovd=NOT_EXECUTED (destination still 0x%08x)\n", out);
    return 0;
}
