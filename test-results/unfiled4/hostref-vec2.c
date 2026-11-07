// hostref-vec2.c — native (aarch64) second pass for the vectored
// read family: fresh descriptor per case (no shared file position),
// plus range-threshold and per-segment discriminators. The VFS /
// iov-import code under test is arch-generic; what is measured
// here is the same kernel code an x86-64 host runs. Raw syscalls
// via syscall() only.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <stdint.h>

static char buf[128];
static long pv(int fd, struct iovec* iov, int cnt, int64_t off) {
    errno = 0; return syscall(SYS_preadv, fd, iov, cnt,
        (unsigned long)((uint64_t)off & 0xffffffffULL),
        (unsigned long)((uint64_t)off >> 32));
}
static void show(const char* name, long r) {
    if (r < 0) printf("%-52s -> -1 errno=%d (%s)\n", name, errno, strerror(errno));
    else printf("%-52s -> %ld\n", name, r);
    fflush(stdout);
}
#define SEG(b, l) ((struct iovec){ (b), (size_t)(l) })
static int freshFd() {
    int fd = open("/tmp/hostref-data.bin", O_RDONLY);
    if (fd < 0) { perror("open data"); exit(1); }
    return fd;
}
int main() {
    struct iovec v[2];
    int fd;
    v[0] = SEG(buf, 1ULL << 48);
    fd = freshFd(); show("preadv single len=2^48", pv(fd, v, 1, 0)); close(fd);
    v[0] = SEG(buf, 1ULL << 44);
    fd = freshFd(); show("preadv single len=2^44", pv(fd, v, 1, 0)); close(fd);
    v[0] = SEG(buf, 8); v[1] = SEG(buf, (1ULL << 63) - 4);
    fd = freshFd(); show("preadv {8, 2^63-4} (seg2 top-bit)", pv(fd, v, 2, 0)); close(fd);
    v[0] = SEG(buf, 1ULL << 63);
    fd = freshFd(); show("preadv {2^63} single (top-bit)", pv(fd, v, 1, 0)); close(fd);
    v[0] = SEG(buf, (1ULL << 63) - 1);
    fd = freshFd(); show("preadv {2^63-1} single (SSIZE_MAX)", pv(fd, v, 1, 0)); close(fd);
    v[0] = SEG(buf, 1ULL << 62); v[1] = SEG(buf, 1ULL << 62);
    fd = freshFd(); show("preadv {2^62, 2^62} sum wraps", pv(fd, v, 2, 0)); close(fd);
    // readv with a fresh descriptor per case.
    v[0] = SEG(buf, 1ULL << 62); v[1] = SEG(buf, 1ULL << 62);
    fd = freshFd(); errno = 0; show("readv {2^62, 2^62} sum wraps", syscall(SYS_readv, fd, v, 2)); close(fd);
    v[0] = SEG(buf, 1ULL << 40); v[1] = SEG(buf, 1ULL << 40);
    fd = freshFd(); errno = 0; show("readv {2^40, 2^40} (control)", syscall(SYS_readv, fd, v, 2)); close(fd);
    v[0] = SEG(buf, 8); v[1] = SEG(buf, (1ULL << 63) - 4);
    fd = freshFd(); errno = 0; show("readv {8, 2^63-4} (seg2 top-bit)", syscall(SYS_readv, fd, v, 2)); close(fd);

    // Pipe: order of ESPIPE against the remaining checks.
    int p[2];
    if (pipe(p) != 0) { perror("pipe"); return 1; }
    if (write(p[1], "wxyz", 4) != 4) { perror("pipe write"); return 1; }
    struct iovec vp[1]; vp[0] = SEG(buf, 4);
    errno = 0; show("pread64 pipe off=-2", syscall(SYS_pread64, p[0], buf, 4, -2));
    show("preadv pipe iovcnt=-1", pv(p[0], vp, -1, 0));
    show("preadv closed-fd control", pv(9999, vp, 1, 0));
    return 0;
}
