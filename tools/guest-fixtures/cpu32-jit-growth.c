// Chromium's scalar SSE -> x87 truncation sequence used for buffer growth,
// plus an 80-bit precision witness: ((1 + 2^-63) - 1) * 2^63 must equal 1.
// The combined runtime requests 80-bit x87 state; double-cache codegen loses
// that low bit once this function gets hot, even if ordinary conversions pass.
#include <stdint.h>
__asm__(".global _start\n_start:\n andl $-16,%esp\n call guest_main\n"
        " movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");

static void report(const char* text, unsigned size) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(size) : "memory", "cc");
}

__attribute__((noinline)) static int64_t grow(int count) {
    const double factor = 1.5;
    int64_t result;
    __asm__ volatile("cvtsi2sd %1,%%xmm1; mulsd %2,%%xmm1; movsd %%xmm1,%0; fldl %0; fisttpll %0"
        : "=m"(result) : "r"(count), "m"(factor) : "xmm1", "memory");
    return result;
}

// Retain Chromium's flags/CMOV/IMUL sequence around the conversion as well.
__asm__(".data\n.Lstride:\n.long 4\n.Lfactor:\n.long 0,0x3ff80000\n.Lone:\n.long 0,0x3ff00000\n"
        ".Lextended:\n.long 1,0x80000000\n.word 0x3fff\n.Ltwo63:\n.long 0,0x43e00000\n"
        ".text\n.type convert_constant,@function\nconvert_constant:\n"
        "cvttsd2si .Lfactor,%eax\n ret\n"
        ".type precision_check,@function\nprecision_check:\n subl $8,%esp\n fninit\n"
        "fld1\n fldt .Lextended\n fsubp %st,%st(1)\n fabs\n fmull .Ltwo63\n fisttpll (%esp)\n"
        "movl (%esp),%eax\n addl $8,%esp\n ret\n"
        ".type allocation_bytes,@function\nallocation_bytes:\n"
        "pushl %ebx\n pushl %esi\n pushl %edi\n subl $8,%esp\n"
        "movl $.Lstride,%ecx\n movl 24(%esp),%eax\n movl $0x3fffffff,%edx\n xorl %esi,%esi\n"
        "movl %eax,%edi\n sarl $31,%edi\n movl %esi,%ebx\n cmpl %eax,%edx\n sbbl %edi,%ebx\n jl 2f\n"
        "testl %eax,%eax\n jle 1f\n"
        "movsd .Lfactor,%xmm0\n ucomisd .Lone,%xmm0\n jbe 1f\n"
        "cvtsi2sd %eax,%xmm1\n mulsd %xmm0,%xmm1\n movsd %xmm1,(%esp)\n fldl (%esp)\n fisttpll (%esp)\n"
        "movl (%esp),%edi\n movl %edx,%ebx\n addl $-8,%ebx\n adcl $-1,%esi\n"
        "leal 7(%edi),%eax\n andl $-8,%eax\n cmpl %ebx,%edi\n"
        "movl 4(%esp),%edi\n sbbl %esi,%edi\n cmovgel %edx,%eax\n"
        "1: imull (%ecx),%eax\n jmp 3f\n 2: movl $-1,%eax\n"
        "3: addl $8,%esp\n popl %edi\n popl %esi\n popl %ebx\n ret\n");
extern int allocation_bytes(int count);
extern int precision_check(void);

int guest_main(void) {
    for (int i = 0; i < 2000; i++) {
        if (precision_check() != 1) {
            const char text[] = "cpu32-jit-growth: FAIL 80-bit precision\n";
            report(text, sizeof(text) - 1);
            return 1;
        }
        int converted;
        __asm__ volatile("movl $99,%%eax\n call convert_constant"
            : "=a"(converted) : : "ecx", "edx", "memory", "cc");
        int value = i % 127 + 1;
        int64_t actual = grow(value);
        int64_t expected = value * 3 / 2;
        int expectedBytes = ((expected + 7) & -8) * 4;
        if (converted != 1 || actual != expected || allocation_bytes(value) != expectedBytes) {
            const char cold[] = "cpu32-jit-growth: FAIL cold\n";
            const char hot[] = "cpu32-jit-growth: FAIL hot\n";
            if (i < 200) report(cold, sizeof(cold) - 1);
            else report(hot, sizeof(hot) - 1);
            return 1;
        }
    }
    const char text[] = "cpu32-jit-growth: RESULT 0\n";
    report(text, sizeof(text) - 1);
    return 0;
}
