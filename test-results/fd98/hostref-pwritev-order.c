#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
static long callPwritev2(int fd, struct iovec* iov, int cnt, long off, int flags) {
    errno = 0;
    return syscall(SYS_pwritev2, fd, iov, cnt, (unsigned long)off, (unsigned long)(off >> 32), flags);
}
int main() {
    char tmpl[] = "/tmp/hostref-pwoXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }
    char path[512]; snprintf(path, sizeof(path), "%s/d.bin", dir);
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }
    if (write(fd, "0123456789ABCDEFGHIJ", 20) != 20) return 1;
    int rfd = open(path, O_RDONLY);
    char b[8] = "abcd";
    struct iovec iov = {b, 4};
    long r;
    r = callPwritev2(fd, &iov, -1, 0, 0x40); printf("pwritev2 good fd iovcnt -1 AND bad flags -> ret=%ld errno=%d (%s)\n", r, errno, strerror(errno));
    r = callPwritev2(rfd, &iov, -1, 0, 0); printf("pwritev2 RDONLY fd iovcnt -1 -> ret=%ld errno=%d (%s)\n", r, errno, strerror(errno));
    r = callPwritev2(rfd, &iov, 1, 0, 0x40); printf("pwritev2 RDONLY fd bad flags -> ret=%ld errno=%d (%s)\n", r, errno, strerror(errno));
    r = callPwritev2(fd, &iov, 1, 0, 0x20); printf("pwritev2 flags 0x20 (NOAPPEND) -> ret=%ld errno=%d (%s)\n", r, errno, strerror(errno));
    close(fd); close(rfd);
    unlink(path); rmdir(dir);
    return 0;
}
