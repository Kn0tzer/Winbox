#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
static long callPreadv2(int fd, struct iovec* iov, int cnt, long off, int flags) {
    errno = 0;
    return syscall(SYS_preadv2, fd, iov, cnt, (unsigned long)off, (unsigned long)(off >> 32), flags);
}
int main() {
    char tmpl[] = "/tmp/hostref-po2XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }
    char path[512]; snprintf(path, sizeof(path), "%s/d.bin", dir);
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }
    if (write(fd, "0123456789ABCDEFGHIJ", 20) != 20) return 1;
    char b[8];
    struct iovec iov = {b, 4};
    long r;
    r = callPreadv2(fd, NULL, 1, 0, 0x40); printf("preadv2 NULL iov AND bad flags -> ret=%ld errno=%d (%s)\n", r, errno, strerror(errno));
    r = callPreadv2(fd, (struct iovec*)0x1000, 1, 0, 0x40); printf("preadv2 unmapped iov AND bad flags -> ret=%ld errno=%d (%s)\n", r, errno, strerror(errno));
    r = callPreadv2(fd, &iov, 1025, 0, 0x40); printf("preadv2 iovcnt 1025 AND bad flags -> ret=%ld errno=%d (%s)\n", r, errno, strerror(errno));
    r = callPreadv2(fd, &iov, 1, -2, 0x40); printf("preadv2 offset -2 AND bad flags -> ret=%ld errno=%d (%s)\n", r, errno, strerror(errno));
    close(fd);
    unlink(path); rmdir(dir);
    return 0;
}
