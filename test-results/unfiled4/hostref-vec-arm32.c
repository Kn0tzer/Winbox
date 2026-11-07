// hostref-vec-arm32.c — the 32-bit compat vectored-read contract,
// measured in a real 32-bit process (aarch32 under the kernel's
// compat layer; the compat iovec conversion is arch-generic kernel
// code, the same an ia32 guest reaches). Every measured call is a
// raw svc: ARM EABI numbers readv 145, pread64 180, preadv 333,
// preadv2 377, open 5, write 4, read 3, pipe 42, lseek 19, mmap2 192.
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static long svc6(long nr, long a1, long a2, long a3, long a4, long a5, long a6) {
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a1;
    register long r1 __asm__("r1") = a2;
    register long r2 __asm__("r2") = a3;
    register long r3 __asm__("r3") = a4;
    register long r4 __asm__("r4") = a5;
    register long r5 __asm__("r5") = a6;
    __asm__ volatile("svc 0"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
        : "memory");
    return r0;
}
static void show(const char* name, long r) {
    if (r < 0) printf("%-52s -> %ld (errno %ld)\n", name, r, -r);
    else printf("%-52s -> %ld\n", name, r);
    fflush(stdout);
}
struct iov32 { uint32_t base; uint32_t len; };
static char smallbuf[256];
static struct iov32 v[4];
int main() {
    // Create the data file through raw calls (O_RDWR|O_CREAT|O_TRUNC).
    long fd = svc6(5, (long)"/tmp/hostref-data32.bin", 0x242, 0644, 0, 0, 0);
    if (fd < 0) { printf("open failed %ld\n", fd); return 1; }
    svc6(4, fd, (long)"0123456789ABCDEF", 16, 0, 0, 0);
    // A 3 GiB NORESERVE anonymous mapping to point big segments at.
    long big = svc6(192, 0x10000000, 0xC0000000, 3, 0x02 | 0x20 | 0x4000, -1, 0);
    if (big < 0) {
        printf("mmap2 3GiB failed %ld, retrying addr 0\n", big);
        big = svc6(192, 0, 0xC0000000, 3, 0x02 | 0x20 | 0x4000, -1, 0);
    }
    if (big < 0) { printf("mmap2 failed %ld\n", big); return 1; }
    printf("big mapping at 0x%lx\n", big);

    // readv cases (lseek to 0 before each).
    v[0].base = (uint32_t)big; v[0].len = 0xC0000000U;
    v[1].base = (uint32_t)big; v[1].len = 0xC0000000U;
    svc6(19, fd, 0, 0, 0, 0, 0);
    show("c32 readv {3G, 3G} sum=2^33", svc6(145, fd, (long)v, 2, 0, 0, 0));
    v[0].len = 0x80000000U;
    svc6(19, fd, 0, 0, 0, 0, 0);
    show("c32 readv {2G} single sum=2^31", svc6(145, fd, (long)v, 1, 0, 0, 0));
    v[0].len = 0xFFFFFFFFU;
    svc6(19, fd, 0, 0, 0, 0, 0);
    show("c32 readv {4G-1} single", svc6(145, fd, (long)v, 1, 0, 0, 0));
    v[0].len = 0x40000000U; v[1].len = 0x40000000U;
    svc6(19, fd, 0, 0, 0, 0, 0);
    show("c32 readv {1G, 1G} sum=2^31", svc6(145, fd, (long)v, 2, 0, 0, 0));
    v[0].len = 0x7FFFFFFFU;
    svc6(19, fd, 0, 0, 0, 0, 0);
    show("c32 readv {2G-1} single (fits s32)", svc6(145, fd, (long)v, 1, 0, 0, 0));
    v[0].base = (uint32_t)(uintptr_t)smallbuf; v[0].len = 16;
    svc6(19, fd, 0, 0, 0, 0, 0);
    show("c32 readv {16} control", svc6(145, fd, (long)v, 1, 0, 0, 0));
    // preadv mirror of the wrap case.
    v[0].base = (uint32_t)big; v[0].len = 0xC0000000U;
    v[1].base = (uint32_t)big; v[1].len = 0xC0000000U;
    show("c32 preadv {3G, 3G} off=0", svc6(361, fd, (long)v, 2, 0, 0, 0));

    // Pipe matrix through the compat numbers.
    long p[2];
    if (svc6(42, (long)p, 0, 0, 0, 0, 0) < 0) { printf("pipe failed\n"); return 1; }
    svc6(4, p[1], (long)"wxyz", 4, 0, 0, 0);
    v[0].base = (uint32_t)(uintptr_t)smallbuf; v[0].len = 4;
    show("c32 preadv pipe off=0", svc6(361, p[0], (long)v, 1, 0, 0, 0));
    show("c32 pread64 pipe off=0", svc6(180, p[0], (long)smallbuf, 4, 0, 0, 0));
    memset(smallbuf, 0, 16);
    show("c32 preadv2 pipe off=-1 mode",
         svc6(392, p[0], (long)v, 1, -1, -1, 0));
    printf("  pipe buffer after -1 mode: '%c%c%c%c'\n",
           smallbuf[0], smallbuf[1], smallbuf[2], smallbuf[3]);
    char drain[8] = {0};
    show("c32 plain read after positioned attempts", svc6(3, p[0], (long)drain, 4, 0, 0, 0));
    printf("  drained: '%c%c%c%c'\n", drain[0], drain[1], drain[2], drain[3]);
    return 0;
}
