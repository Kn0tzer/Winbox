// Host reference: preadv/preadv2 validation order, offset -1 semantics,
// flags mask, partial shape, and IN_ACCESS counts.
// Measured on the box host kernel; run natively (x86-64).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/inotify.h>
#include <sys/stat.h>

static int inoFd = -1;
static int wd = -1;

static void drainInotify() {
    char buf[8192];
    int flags = fcntl(inoFd, F_GETFL);
    fcntl(inoFd, F_SETFL, flags | O_NONBLOCK);
    while (read(inoFd, buf, sizeof(buf)) > 0) {}
    fcntl(inoFd, F_SETFL, flags);
}

static int countAccess() {
    char buf[8192];
    int flags = fcntl(inoFd, F_GETFL);
    fcntl(inoFd, F_SETFL, flags | O_NONBLOCK);
    ssize_t total = read(inoFd, buf, sizeof(buf));
    fcntl(inoFd, F_SETFL, flags);
    int n = 0;
    ssize_t off = 0;
    while (total > 0 && off + 16 <= total) {
        unsigned int mask, nameLen;
        memcpy(&mask, buf + off + 4, 4);
        memcpy(&nameLen, buf + off + 12, 4);
        if (mask & IN_ACCESS) n++;
        off += 16 + (ssize_t)nameLen;
    }
    return n;
}

static void report(const char* name, long ret, int err, int access) {
    printf("%-58s ret=%ld errno=%d (%s) in_access=%d\n", name, ret, err, strerror(err), access);
}

static long callPreadv(int fd, struct iovec* iov, int cnt, long off) {
    errno = 0;
    long r = syscall(SYS_preadv, fd, iov, cnt, (unsigned long)off, (unsigned long)(off >> 32));
    return r;
}
static long callPreadv2(int fd, struct iovec* iov, int cnt, long off, int flags) {
    errno = 0;
    long r = syscall(SYS_preadv2, fd, iov, cnt, (unsigned long)off, (unsigned long)(off >> 32), flags);
    return r;
}

int main() {
    char tmpl[] = "/tmp/hostref-preadvXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }
    char path[512];
    snprintf(path, sizeof(path), "%s/data.bin", dir);
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }
    const char* text = "0123456789ABCDEFGHIJ";
    if (write(fd, text, 20) != 20) { perror("write"); return 1; }
    lseek(fd, 0, SEEK_SET);
    int wfd = open(path, O_WRONLY);
    int rfd = open(path, O_RDONLY);
    int dfd = open(dir, O_RDONLY);

    inoFd = inotify_init1(0);
    wd = inotify_add_watch(inoFd, path, IN_ACCESS | IN_MODIFY);
    if (inoFd < 0 || wd < 0) { perror("inotify"); return 1; }

    char b0[8], b1[8], b2[8];
    struct iovec iov[3];
    long r; int e, a;

    // --- basic shape ---
    iov[0] = (struct iovec){b0, 4}; iov[1] = (struct iovec){b1, 4}; iov[2] = (struct iovec){b2, 4};
    drainInotify();
    r = callPreadv(fd, iov, 3, 2); e = errno; a = countAccess();
    report("preadv 3 segs off 2", r, e, a);
    printf("    segs: '%.4s' '%.4s' '%.4s' pos=%ld\n", b0, b1, b2, (long)lseek(fd, 0, SEEK_CUR));

    drainInotify();
    r = callPreadv(fd, iov, 1, 1000); e = errno; a = countAccess();
    report("preadv at EOF (0-byte success)", r, e, a);

    drainInotify();
    r = callPreadv(fd, iov, 0, 0); e = errno; a = countAccess();
    report("preadv iovcnt 0", r, e, a);

    iov[0] = (struct iovec){b0, 0}; iov[1] = (struct iovec){b1, 0};
    drainInotify();
    r = callPreadv(fd, iov, 2, 0); e = errno; a = countAccess();
    report("preadv all-zero-length segs", r, e, a);

    // short read ends walk
    iov[0] = (struct iovec){b0, 4}; iov[1] = (struct iovec){b1, 4};
    memset(b0, 0, 8); memset(b1, 'x', 8);
    drainInotify();
    r = callPreadv(fd, iov, 2, 18); e = errno; a = countAccess();
    report("preadv across EOF (short first seg)", r, e, a);
    printf("    seg0='%.4s' seg1 untouched=%d\n", b0, b1[0] == 'x');

    // partial: good first segment, bad second segment buffer
    iov[0] = (struct iovec){b0, 4}; iov[1] = (struct iovec){(void*)0x1000, 4};
    drainInotify();
    r = callPreadv(fd, iov, 2, 0); e = errno; a = countAccess();
    report("preadv good seg + bad-buffer seg", r, e, a);

    // failing first segment buffer
    iov[0] = (struct iovec){(void*)0x1000, 4};
    drainInotify();
    r = callPreadv(fd, iov, 1, 0); e = errno; a = countAccess();
    report("preadv bad-buffer first seg", r, e, a);

    // --- offset semantics ---
    iov[0] = (struct iovec){b0, 4};
    drainInotify();
    r = callPreadv(fd, iov, 1, -1); e = errno; a = countAccess();
    report("preadv good fd offset -1", r, e, a);

    r = callPreadv(fd, iov, 1, -2); e = errno; a = countAccess();
    report("preadv good fd offset -2", r, e, a);

    lseek(fd, 5, SEEK_SET);
    memset(b0, 0, 8);
    drainInotify();
    r = callPreadv2(fd, iov, 1, -1, 0); e = errno; a = countAccess();
    report("preadv2 good fd offset -1 flags 0 (pos was 5)", r, e, a);
    printf("    seg='%.4s' pos after=%ld\n", b0, (long)lseek(fd, 0, SEEK_CUR));

    r = callPreadv2(fd, iov, 1, -2, 0); e = errno; a = countAccess();
    report("preadv2 good fd offset -2", r, e, a);
    lseek(fd, 0, SEEK_SET);

    // --- ordering discriminators ---
    r = callPreadv(9999, iov, 1, -1); e = errno; a = countAccess();
    report("preadv BAD fd offset -1", r, e, a);
    r = callPreadv(9999, iov, 1, -2); e = errno; a = countAccess();
    report("preadv BAD fd offset -2", r, e, a);
    r = callPreadv(9999, iov, 1, 0); e = errno; a = countAccess();
    report("preadv BAD fd offset 0", r, e, a);
    r = callPreadv2(9999, iov, 1, -1, 0); e = errno; a = countAccess();
    report("preadv2 BAD fd offset -1", r, e, a);
    r = callPreadv2(9999, iov, 1, -2, 0); e = errno; a = countAccess();
    report("preadv2 BAD fd offset -2", r, e, a);
    r = callPreadv2(9999, iov, 1, 0, 0x40); e = errno; a = countAccess();
    report("preadv2 BAD fd bad flags 0x40", r, e, a);
    r = callPreadv2(fd, iov, 1, -2, 0x40); e = errno; a = countAccess();
    report("preadv2 good fd offset -2 AND bad flags", r, e, a);
    r = callPreadv2(wfd, iov, 1, 0, 0x40); e = errno; a = countAccess();
    report("preadv2 WRONLY fd bad flags", r, e, a);
    r = callPreadv(wfd, iov, 1, 0); e = errno; a = countAccess();
    report("preadv WRONLY fd offset 0", r, e, a);
    r = callPreadv(wfd, iov, 1, -1); e = errno; a = countAccess();
    report("preadv WRONLY fd offset -1", r, e, a);
    r = callPreadv(wfd, iov, 0, 0); e = errno; a = countAccess();
    report("preadv WRONLY fd iovcnt 0", r, e, a);
    r = callPreadv(wfd, iov, -1, 0); e = errno; a = countAccess();
    report("preadv WRONLY fd iovcnt -1", r, e, a);
    r = callPreadv2(fd, iov, -1, 0, 0x40); e = errno; a = countAccess();
    report("preadv2 good fd iovcnt -1 AND bad flags", r, e, a);
    r = callPreadv(fd, NULL, -1, 0); e = errno; a = countAccess();
    report("preadv iovcnt -1 NULL iov", r, e, a);
    r = callPreadv(fd, NULL, 1, 0); e = errno; a = countAccess();
    report("preadv iovcnt 1 NULL iov", r, e, a);
    r = callPreadv(fd, (struct iovec*)0x1000, 1, 0); e = errno; a = countAccess();
    report("preadv iovcnt 1 unmapped iov", r, e, a);
    r = callPreadv(fd, iov, 1025, 0); e = errno; a = countAccess();
    report("preadv iovcnt 1025", r, e, a);

    // --- flags mask on reads (preadv2, good fd) ---
    int bits[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40};
    for (int i = 0; i < 7; i++) {
        char name[64];
        snprintf(name, sizeof(name), "preadv2 flags 0x%02x", bits[i]);
        drainInotify();
        r = callPreadv2(fd, iov, 1, 0, bits[i]); e = errno; a = countAccess();
        report(name, r, e, a);
    }

    // --- non-file descriptors ---
    r = callPreadv(dfd, iov, 1, 0); e = errno; a = countAccess();
    report("preadv directory fd", r, e, a);
    int pfd[2];
    if (pipe(pfd) == 0) {
        if (write(pfd[1], "pipe", 4) != 4) perror("pipe write");
        r = callPreadv(pfd[0], iov, 1, 0); e = errno; a = countAccess();
        report("preadv pipe fd", r, e, a);
        close(pfd[0]); close(pfd[1]);
    }

    // --- position never moves for preadv (explicit offsets) ---
    lseek(rfd, 3, SEEK_SET);
    iov[0] = (struct iovec){b0, 4};
    r = callPreadv(rfd, iov, 1, 10); e = errno;
    printf("preadv explicit offset: ret=%ld pos after=%ld (was 3)\n", r, (long)lseek(rfd, 0, SEEK_CUR));

    close(fd); close(wfd); close(rfd); close(dfd); close(inoFd);
    unlink(path);
    rmdir(dir);
    return 0;
}
