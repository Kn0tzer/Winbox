// Branch chaining must preserve flags, a bounded host stack, and termination.
#include <stdint.h>

__asm__(".global _start\n_start:\n movl %esp, %eax\n andl $-16, %esp\n"
        " subl $12, %esp\n pushl %eax\n call guest_main\n"
        " movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");

static void say(const char *text, unsigned length) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(length) : "memory", "cc");
}

#define CHECK_CC(cc, expected) do { \
    unsigned taken; \
    __asm__ volatile("pushl %1; popfl; movl $0, %0; j" #cc " 1f; jmp 2f; 1: movl $1, %0; 2:" \
        : "=&r"(taken) : "r"(flags) : "cc", "memory"); \
    if (taken != (unsigned)(expected)) return 1; \
} while (0)

__attribute__((noinline, noclone)) static unsigned step(unsigned value) {
    return value + 7;
}

int guest_main(unsigned *stack) {
    for (unsigned bits = 0; bits < 32; ++bits) {
        unsigned c = bits & 1, p = (bits >> 1) & 1, z = (bits >> 2) & 1;
        unsigned s = (bits >> 3) & 1, o = (bits >> 4) & 1;
        unsigned flags = 0x202 | c | (p << 2) | (z << 6) | (s << 7) | (o << 11);
        CHECK_CC(o, o); CHECK_CC(no, !o);
        CHECK_CC(b, c); CHECK_CC(ae, !c);
        CHECK_CC(e, z); CHECK_CC(ne, !z);
        CHECK_CC(be, z || c); CHECK_CC(a, !z && !c);
        CHECK_CC(s, s); CHECK_CC(ns, !s);
        CHECK_CC(p, p); CHECK_CC(np, !p);
        CHECK_CC(l, s != o); CHECK_CC(ge, s == o);
        CHECK_CC(le, z || s != o); CHECK_CC(g, !z && s == o);
    }
    unsigned remaining = 1000000;
    __asm__ volatile("1: decl %0; jnz 1b" : "+r"(remaining) : : "cc");
    if (remaining) return 2;
    unsigned (*volatile function)(unsigned) = step;
    unsigned sum = 5;
    for (unsigned i = 0; i < 10000; ++i) sum = function(sum);
    if (sum != 70005) return 3;
    static const char ok[] = "cpu32-branches: FLAGS LOOP CALLS RESULT 0\n";
    say(ok, sizeof(ok) - 1);
    if (stack[0] > 1) {
        // No syscalls or returns in this loop: kill must cross a Jcc boundary.
        __asm__ volatile("xorl %%eax, %%eax; 1: testl %%eax, %%eax; jz 1b" : : : "eax", "cc");
    }
    return 0;
}
