// hostref-vec32.c — host reference for the 32-bit (compat) vectored
// read family. Issued with int 0x80 from a 64-bit process, which
// enters the kernel's ia32 syscall table and its compat handlers —
// the same handlers a 32-bit process reaches. All guest-visible
// pointers live below 4 GiB (MAP_32BIT) as they would for a 32-bit
// caller. i386 numbers: readv 145, pread64 180, preadv 333,
// preadv2 377.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#ifndef MAP_32BIT
#define MAP_32BIT 0x40
#endif

static long sys32(long nr, uint32_t a1, uint32_t a2, uint32_t a3,
                  uint32_t a4, uint32_t a5, uint32_t a6) {
    long ret;
    register uint32_t rbpv __asm__("rbp") = a6;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(nr), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5), "r"(rbpv)
        : "rcx", "r11", "memory");
    return ret;
}
struct iov32 { uint32_t base; uint32_t len; };
static void show(const char* name, long r) {
    if (r < 0) printf("%-52s -> %ld (errno %ld: %s)\n", name, r, -r, strerror((int)-r));
    else printf("%-52s -> %ld\n", name, r);
    fflush(stdout);
}
static void* low32(size_t n) {
    void* p = mmap(NULL, n, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p == MAP_FAILED) { perror("mmap32"); exit(1); }
    return p;
}
int main() {
    int fd = open("/tmp/hostref-data.bin", O_RDWR);
    if (fd < 0) { perror("open data"); return 1; }
    char* buf = low32(4096);
    struct iov32* v = low32(4096);

    v[0].base = (uint32_t)(uintptr_t)buf; v[0].len = 0xC0000000U;
    v[1].base = (uint32_t)(uintptr_t)buf; v[1].len = 0xC0000000U;
    show("ia32 readv sum=2^33 (2 x 3 GiB)",
         sys32(145, fd, (uint32_t)(uintptr_t)v, 2, 0, 0, 0));
    v[0].len = 0x7FFFFFFFU; v[1].len = 1;
    show("ia32 readv sum=2^31",
         sys32(145, fd, (uint32_t)(uintptr_t)v, 2, 0, 0, 0));
    v[0].len = 0x7FFFFFFFU;
    show("ia32 readv sum=2^31-1 (single seg)",
         sys32(145, fd, (uint32_t)(uintptr_t)v, 1, 0, 0, 0));
    v[0].len = 0x80000000U;
    show("ia32 readv sum=2^31 (single seg)",
         sys32(145, fd, (uint32_t)(uintptr_t)v, 1, 0, 0, 0));
    v[0].len = 0xFFFFFFFFU; v[1].len = 0xFFFFFFFFU;
    show("ia32 readv sum=2^33-2 (2 x 4 GiB-1)",
         sys32(145, fd, (uint32_t)(uintptr_t)v, 2, 0, 0, 0));
    v[0].len = 0x40000000U; v[1].len = 0x40000000U;
    show("ia32 readv sum=2^31 (2 x 1 GiB)",
         sys32(145, fd, (uint32_t)(uintptr_t)v, 2, 0, 0, 0));
    v[0].len = 16; v[1].len = 16;
    show("ia32 readv sum=32 (control)",
         sys32(145, fd, (uint32_t)(uintptr_t)v, 2, 0, 0, 0));

    v[0].len = 0xC0000000U; v[1].len = 0xC0000000U;
    show("ia32 preadv sum=2^33 off=0",
         sys32(333, fd, (uint32_t)(uintptr_t)v, 2, 0, 0, 0));

    // Pipe matrix through the compat numbers.
    int p[2];
    if (pipe(p) != 0) { perror("pipe"); return 1; }
    if (write(p[1], "wxyz", 4) != 4) { perror("pipe write"); return 1; }
    v[0].base = (uint32_t)(uintptr_t)buf; v[0].len = 4;
    show("ia32 preadv pipe off=0",
         sys32(333, p[0], (uint32_t)(uintptr_t)v, 1, 0, 0, 0));
    show("ia32 pread64 pipe off=0",
         sys32(180, p[0], (uint32_t)(uintptr_t)buf, 4, 0, 0, 0));
    memset(buf, 0, 16);
    show("ia32 preadv2 pipe off=-1 (file-pos mode)",
         sys32(377, p[0], (uint32_t)(uintptr_t)v, 1, 0xFFFFFFFFU, 0xFFFFFFFFU, 0));
    printf("  pipe buffer after -1 mode: '%c%c%c%c'\n", buf[0], buf[1], buf[2], buf[3]);
    char drain[8] = {0};
    long dr = read(p[0], drain, 4);
    show("plain read after positioned attempts", dr);
    printf("  drained: '%c%c%c%c'\n", drain[0], drain[1], drain[2], drain[3]);
    return 0;
}
