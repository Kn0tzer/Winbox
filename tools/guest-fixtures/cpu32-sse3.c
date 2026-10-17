// Run the instruction bytes used by DXVK, including an eight-byte page tail.
#include <stdint.h>

// Keep this CPU check independent of static-libc startup and IFUNC dispatch.
__asm__(".global _start\n_start:\n andl $-16, %esp\n call guest_main\n"
        " movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");

static unsigned syscall3(unsigned number, unsigned a, unsigned b, unsigned c) {
    unsigned result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "b"(a), "c"(b), "d"(c) : "memory", "cc");
    return result;
}

static unsigned map_pages(void) {
    unsigned result;
    __asm__ volatile("pushl %%ebp; xorl %%ebp, %%ebp; int $0x80; popl %%ebp"
                     : "=a"(result) : "a"(192), "b"(0), "c"(8192), "d"(3), "S"(34), "D"(~0U) : "memory", "cc");
    return result;
}

static int check_instructions(void) {
    const uint64_t input[2] = {0xaabbccddeeff2468ULL, 0x1122334455667788ULL};
    uint64_t output[2];
    unsigned flags;
    __asm__ volatile("pushl $0x8d5; popfl; movdqu (%2), %%xmm0; movddup %%xmm0, %%xmm0;"
                     "movdqu %%xmm0, %0; pushfl; popl %1"
                     : "=m"(output), "=r"(flags) : "r"(input) : "xmm0", "cc", "memory");
    if (output[0] != input[0] || output[1] != input[0] || (flags & 0x8d5) != 0x8d5) return 1;

    unsigned char unaligned[19];
    for (unsigned i = 0; i < sizeof(input); ++i) unaligned[i + 3] = ((const unsigned char*)input)[i];
    __asm__ volatile("movsldup (%1), %%xmm0; movdqu %%xmm0, %0"
                     : "=m"(output) : "r"(unaligned + 3) : "xmm0", "memory");
    if (output[0] != 0xeeff2468eeff2468ULL || output[1] != 0x5566778855667788ULL) return 2;
    __asm__ volatile("movshdup (%1), %%xmm0; movdqu %%xmm0, %0"
                     : "=m"(output) : "r"(unaligned + 3) : "xmm0", "memory");
    if (output[0] != 0xaabbccddaabbccddULL || output[1] != 0x1122334411223344ULL) return 3;

    const unsigned page = 4096;
    unsigned address = map_pages();
    unsigned char *mapping = (unsigned char*)(uintptr_t)address;
    if (address >= 0xfffff000U || syscall3(125, address + page, page, 0)) return 4;
    uint64_t mask = ~0ULL;
    for (unsigned i = 0; i < 8; ++i) mapping[page - 8 + i] = ((const unsigned char*)&mask)[i];
    __asm__ volatile("movddup (%1), %%xmm0; movdqu %%xmm0, %0"
                     : "=m"(output) : "r"(mapping + page - 8) : "xmm0", "memory");
    if (output[0] != mask || output[1] != mask) return 5;
    const uint64_t allocation[2] = {3145728, 256};
    __asm__ volatile("movdqu (%1), %%xmm0; movddup (%2), %%xmm1; pand %%xmm1, %%xmm0; movdqu %%xmm0, %0"
                     : "=m"(output) : "r"(allocation), "r"(mapping + page - 8) : "xmm0", "xmm1", "memory");
    if (output[0] != allocation[0] || output[1] != allocation[1]) return 6;
    // Exact Chromium startup fault: HADDPD xmm7,xmm7, plus a memory source.
    const uint64_t doubles[2] __attribute__((aligned(16))) = {0x3ff4000000000000ULL, 0xc00c000000000000ULL}; // 1.25, -3.5
    const uint64_t other[2] __attribute__((aligned(16))) = {0x4024000000000000ULL, 0x3fe0000000000000ULL}; // 10, 0.5
    __asm__ volatile("movupd (%1), %%xmm7; haddpd %%xmm7, %%xmm7; movupd %%xmm7, %0"
                     : "=m"(output) : "r"(doubles) : "xmm7", "memory");
    if (output[0] != 0xc002000000000000ULL || output[1] != 0xc002000000000000ULL) return 7;
    __asm__ volatile("movupd (%1), %%xmm0; haddpd (%2), %%xmm0; movupd %%xmm0, %0"
                     : "=m"(output) : "r"(doubles), "r"(other) : "xmm0", "memory");
    if (output[0] != 0xc002000000000000ULL || output[1] != 0x4025000000000000ULL) return 8;
    // Compiler SIMD must preserve operand selection for NaNs and signed zero.
    const uint32_t left[4] = {0x80000000U, 0, 0x7fc12345U, 0x3f800000U};
    const uint32_t right[4] = {0, 0x80000000U, 0x40000000U, 0x7fc54321U};
    uint32_t lanes[4];
    __asm__ volatile("movups (%1), %%xmm0; movups (%2), %%xmm1; minps %%xmm1, %%xmm0; movups %%xmm0, %0"
                     : "=m"(lanes) : "r"(left), "r"(right) : "xmm0", "xmm1", "memory");
    for (unsigned i = 0; i < 4; ++i) if (lanes[i] != right[i]) return 9;
    __asm__ volatile("movups (%1), %%xmm0; movups (%2), %%xmm1; maxps %%xmm1, %%xmm0; movups %%xmm0, %0"
                     : "=m"(lanes) : "r"(left), "r"(right) : "xmm0", "xmm1", "memory");
    for (unsigned i = 0; i < 4; ++i) if (lanes[i] != right[i]) return 9;
    const uint32_t factors[4] = {0xc0000000U, 0xc0000000U, 0x3f800000U, 0xc0000000U};
    const uint32_t special[4] = {0, 0x80000000U, 1, 0x7f800000U};
    const uint32_t product[4] = {0x80000000U, 0, 1, 0xff800000U};
    __asm__ volatile("movups (%1), %%xmm0; mulps (%2), %%xmm0; movups %%xmm0, %0"
                     : "=m"(lanes) : "r"(special), "r"(factors) : "xmm0", "memory");
    for (unsigned i = 0; i < 4; ++i) if (lanes[i] != product[i]) return 9;
    const uint32_t invalid_ints[4] = {0x7fc12345U, 0x7f800000U, 0x4f000000U, 0xcf000000U};
    __asm__ volatile("movups (%1), %%xmm0; cvttps2dq %%xmm0, %%xmm0; movups %%xmm0, %0"
                     : "=m"(lanes) : "r"(invalid_ints) : "xmm0", "memory");
    for (unsigned i = 0; i < 4; ++i) if (lanes[i] != 0x80000000U) return 9;
    for (unsigned i = 0; i < 16; ++i) mapping[page - 16 + i] = ((const unsigned char*)left)[i];
    __asm__ volatile("movups (%1), %%xmm0; minps (%2), %%xmm0; movups %%xmm0, %0"
                     : "=m"(lanes) : "r"(right), "r"(mapping + page - 16) : "xmm0", "memory");
    for (unsigned i = 0; i < 4; ++i) if (lanes[i] != left[i]) return 9;
    syscall3(91, address, page * 2, 0);
    return 0;
}

int guest_main(void) {
    int result = check_instructions();
    char message[] = "cpu32-sse3: MOVDDUP MOVSLDUP MOVSHDUP HADDPD SIMD_EDGES PAGE_GUARD RESULT 0\n";
    message[sizeof(message) - 3] = '0' + result;
    syscall3(4, 1, (unsigned)(uintptr_t)message, sizeof(message) - 1);
    return result;
}
