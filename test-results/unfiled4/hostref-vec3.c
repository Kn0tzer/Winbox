// hostref-vec3.c — native addenda: top-bit segment in second
// position, and the first position, for the 64-bit vectored read.
#define _GNU_SOURCE
#include <stdio.h>
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
int main() {
    struct iovec v[2];
    int fd = open("/tmp/hostref-data.bin", O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }
    v[0] = SEG(buf, 8); v[1] = SEG(buf, 1ULL << 63);
    show("preadv {8, 2^63} (top-bit seg2)", pv(fd, v, 2, 0));
    v[0] = SEG(buf, 1ULL << 63); v[1] = SEG(buf, 8);
    show("preadv {2^63, 8} (top-bit seg1)", pv(fd, v, 2, 0));
    v[0] = SEG(buf, 8); v[1] = SEG(buf, (1ULL << 63) - 1);
    show("preadv {8, SSIZE_MAX} (sum wraps)", pv(fd, v, 2, 0));
    return 0;
}
