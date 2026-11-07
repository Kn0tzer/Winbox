// hostref-vec.c — RAW-syscall host reference for the vectored-read
// family (64-bit). Every measured call goes through syscall() with
// the bare number: no libc wrapper validates anything first, and
// strace spot-checks confirm the calls enter the kernel.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <stdint.h>

static char buf[128];
static long rv(int fd, struct iovec* iov, int cnt) {
    errno = 0; return syscall(SYS_readv, fd, iov, cnt);
}
static long pv(int fd, struct iovec* iov, int cnt, int64_t off) {
    errno = 0; return syscall(SYS_preadv, fd, iov, cnt,
        (unsigned long)((uint64_t)off & 0xffffffffULL),
        (unsigned long)((uint64_t)off >> 32));
}
static long pv2(int fd, struct iovec* iov, int cnt, int64_t off, int flags) {
    errno = 0; return syscall(SYS_preadv2, fd, iov, cnt,
        (unsigned long)((uint64_t)off & 0xffffffffULL),
        (unsigned long)((uint64_t)off >> 32), flags);
}
static long pr(int fd, void* b, size_t n, int64_t off) {
    errno = 0; return syscall(SYS_pread64, fd, b, n, off);
}
static void show(const char* name, long r) {
    if (r < 0) printf("%-52s -> -1 errno=%d (%s)\n", name, errno, strerror(errno));
    else printf("%-52s -> %ld\n", name, r);
    fflush(stdout);
}
#define SEG(b, l) ((struct iovec){ (b), (size_t)(l) })
int main() {
    int fd = open("/tmp/hostref-data.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open data"); return 1; }
    if (write(fd, "0123456789ABCDEF", 16) != 16) { perror("write data"); return 1; }

    struct iovec v1[2];
    v1[0] = SEG(buf, 1ULL << 62); v1[1] = SEG(buf, 1ULL << 62);
    show("readv sum=2^63 (overflow)", rv(fd, v1, 2));
    struct iovec v2[1]; v2[0] = SEG(buf, (1ULL << 63) - 1);
    show("readv sum=SSIZE_MAX", rv(fd, v2, 1));
    struct iovec v3[1]; v3[0] = SEG(buf, 1ULL << 63);
    show("readv sum=SSIZE_MAX+1 (single seg)", rv(fd, v3, 1));
    struct iovec v4[2];
    v4[0] = SEG(buf, 1ULL << 40); v4[1] = SEG(buf, 1ULL << 40);
    show("readv sum=2^41", rv(fd, v4, 2));
    struct iovec v5[2];
    v5[0] = SEG(buf, 8); v5[1] = SEG(buf, (1ULL << 63) - 4);
    show("readv sum=2^63+3 (overflow)", rv(fd, v5, 2));
    struct iovec v6[2];
    v6[0] = SEG(buf, (1ULL << 63) - 8); v6[1] = SEG(buf, 8);
    show("readv sum=2^63 (overflow, boundary)", rv(fd, v6, 2));
    struct iovec v7[2];
    v7[0] = SEG(buf, (1ULL << 63) - 8); v7[1] = SEG(buf, 7);
    show("readv sum=SSIZE_MAX (two segs)", rv(fd, v7, 2));

    show("preadv sum=2^63 (overflow)", pv(fd, v1, 2, 0));
    show("preadv sum=2^41", pv(fd, v4, 2, 0));

    // MAX_RW_COUNT cap: one readv against /dev/zero asking past the cap.
    size_t bigLen = 0x80001000ULL; // 2 GiB + 4 KiB
    void* big = mmap(NULL, bigLen, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (big == MAP_FAILED) { perror("mmap big"); return 1; }
    int z = open("/dev/zero", O_RDONLY);
    struct iovec vz[1]; vz[0] = SEG(big, 0x80000000ULL);
    show("readv /dev/zero single seg 2^31", rv(z, vz, 1));
    struct iovec vz2[2];
    vz2[0] = SEG(big, 0x40000000ULL); vz2[1] = SEG((char*)big + 0x40000000, 0x40000000ULL);
    show("readv /dev/zero two segs sum=2^31", rv(z, vz2, 2));
    close(z);

    // Pipe matrix.
    int p[2];
    if (pipe(p) != 0) { perror("pipe"); return 1; }
    if (write(p[1], "wxyz", 4) != 4) { perror("pipe write"); return 1; }
    struct iovec vp[1]; vp[0] = SEG(buf, 4);
    show("preadv pipe off=0", pv(p[0], vp, 1, 0));
    show("preadv pipe off=-2", pv(p[0], vp, 1, -2));
    show("preadv pipe iovcnt=2000", pv(p[0], vp, 2000, 0));
    show("preadv pipe iov=NULL cnt=1", pv(p[0], NULL, 1, 0));
    show("preadv2 pipe flags=0x80", pv2(p[0], vp, 1, 0, 0x80));
    memset(buf, 0, sizeof(buf));
    show("preadv2 pipe off=-1 (file-pos mode)", pv2(p[0], vp, 1, -1, 0));
    printf("  pipe buffer after -1 mode: '%c%c%c%c'\n", buf[0], buf[1], buf[2], buf[3]);
    if (write(p[1], "wxyz", 4) != 4) { perror("pipe write2"); return 1; }
    show("pread64 pipe off=0", pr(p[0], buf, 4, 0));
    show("preadv pipe WRITE end", pv(p[1], vp, 1, 0));
    // Was anything consumed by the failing positioned reads?
    char drain[8] = {0};
    errno = 0;
    long dr = syscall(SYS_read, p[0], drain, 4);
    show("plain read after positioned attempts", dr);
    printf("  drained: '%c%c%c%c'\n", drain[0], drain[1], drain[2], drain[3]);
    return 0;
}
