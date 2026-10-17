#include <windows.h>
#include <stdio.h>

__attribute__((noinline)) static unsigned stack_sum(unsigned depth) {
    volatile unsigned saved = depth * 7;
    return depth ? saved + stack_sum(depth - 1) : saved;
}

int main(void) {
    const unsigned pages = 32, page_size = 4096;
    unsigned char *memory = VirtualAlloc(NULL, pages * page_size,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) return 1;
    for (unsigned page = 0; page < pages; ++page) {
        if (*(unsigned *)(memory + page * page_size + 16) != 0) return 2;
        *(unsigned *)(memory + page * page_size + 16) = 0x12340000 + page;
    }
    for (unsigned page = 0; page < pages; ++page) {
        if (*(unsigned *)(memory + page * page_size + 16) != 0x12340000 + page) return 3;
        if (*(unsigned *)(memory + page * page_size + 32) != 0) return 4;
    }
    unsigned sum = stack_sum(32);
    if (sum != 3696) return 5;
    if (!VirtualFree(memory, 0, MEM_RELEASE)) return 6;
    printf("BW32_PROGRAM_OK pages=%u stack_sum=%u pointer_bits=%u\n",
        pages, sum, (unsigned)(8 * sizeof(void *)));
    fflush(stdout);
    return 0;
}
