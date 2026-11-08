// hostref-vec-arm32b.c — compat sum discriminator: every segment
// individually fits a signed 32-bit length, only the SUM crosses
// 2^31. Raw svc readv (145) / lseek (19) / open (5) / mmap2 (192).
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
    __asm__ volatile("svc 0" : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7) : "memory");
    return r0;
}
static void show(const char* name, long r) {
    if (r < 0) printf("%-52s -> %ld (errno %ld)\n", name, r, -r);
    else printf("%-52s -> %ld\n", name, r);
    fflush(stdout);
}
struct iov32 { uint32_t base; uint32_t len; };
static struct iov32 v[4];
int main() {
    long fd = svc6(5, (long)"/tmp/hostref-data32.bin", 0, 0, 0, 0, 0);
    if (fd < 0) { printf("open failed %ld\n", fd); return 1; }
    long big = svc6(192, 0x10000000, 0xC0000000, 3, 0x02 | 0x20 | 0x4000, -1, 0);
    if (big < 0) { printf("mmap2 failed %ld\n", big); return 1; }
    v[0].base = (uint32_t)big; v[0].len = 0x40000000U;
    v[1].base = (uint32_t)big; v[1].len = 0x40000000U;
    v[2].base = (uint32_t)big; v[2].len = 1;
    svc6(19, fd, 0, 0, 0, 0, 0);
    show("c32 readv {1G, 1G, 1} sum=2^31+1", svc6(145, fd, (long)v, 3, 0, 0, 0));
    v[0].len = 0x7FFFFFFFU; v[1].len = 0x7FFFFFFFU;
    svc6(19, fd, 0, 0, 0, 0, 0);
    show("c32 readv {2G-1, 2G-1} sum=2^32-2", svc6(145, fd, (long)v, 2, 0, 0, 0));
    return 0;
}
