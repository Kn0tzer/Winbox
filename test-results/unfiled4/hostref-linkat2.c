// hostref-linkat2.c — the linkat contract, measured with the raw
// syscall in both widths: compiled natively (64-bit) it issues
// SYS_linkat; compiled for aarch32 it issues the compat number 303
// with a raw svc. Setup and result observation use libc and are
// not part of the measurement.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <stdint.h>

#ifdef __arm__
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
static long raw_linkat(int olddf, const char* oldp, int newdf, const char* newp, int flags) {
    return svc6(330, olddf, (long)oldp, newdf, (long)newp, flags, 0);
}
#else
static long raw_linkat(int olddf, const char* oldp, int newdf, const char* newp, int flags) {
    errno = 0;
    long r = syscall(SYS_linkat, olddf, oldp, newdf, newp, flags);
    return r < 0 ? -errno : r;
}
#endif
static void show(const char* name, long r) {
    if (r < 0) printf("%-52s -> %ld (errno %ld: %s)\n", name, r, -r, strerror((int)-r));
    else printf("%-52s -> %ld\n", name, r);
    fflush(stdout);
}
static void exists(const char* path, const char* what) {
    struct stat st;
    if (lstat(path, &st) == 0)
        printf("  %s: exists mode=%o nlink=%lu\n", what, (unsigned)st.st_mode, (unsigned long)st.st_nlink);
    else
        printf("  %s: absent\n", what);
}
int main() {
    char dir[] = "/tmp/hostref-linkat2XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    if (chdir(dir) != 0) { perror("chdir"); return 1; }
    int f = open("src", O_RDWR | O_CREAT, 0644); if (f < 0) { perror("src"); return 1; }
    if (write(f, "data", 4) != 4) { perror("write"); return 1; }
    if (mkdir("sub", 0755) != 0) { perror("mkdir sub"); return 1; }
    int sf = open("sub/sfile", O_RDWR | O_CREAT, 0644); if (sf < 0) { perror("sfile"); return 1; }
    close(sf);
    if (mkdir("srcdir", 0755) != 0) { perror("mkdir srcdir"); return 1; }
    if (symlink("src", "slink") != 0) { perror("symlink"); return 1; }

    show("linkat basic", raw_linkat(AT_FDCWD, "src", AT_FDCWD, "dst", 0));
    exists("dst", "dst"); exists("src", "src");
    show("linkat missing source", raw_linkat(AT_FDCWD, "nosuch", AT_FDCWD, "dst2", 0));
    show("linkat existing target", raw_linkat(AT_FDCWD, "src", AT_FDCWD, "dst", 0));
    show("linkat directory source", raw_linkat(AT_FDCWD, "srcdir", AT_FDCWD, "dst3", 0));
    show("linkat null oldpath", raw_linkat(AT_FDCWD, NULL, AT_FDCWD, "dst4", 0));
    show("linkat null newpath", raw_linkat(AT_FDCWD, "src", AT_FDCWD, NULL, 0));
    show("linkat empty oldpath", raw_linkat(AT_FDCWD, "", AT_FDCWD, "dst5", 0));
    show("linkat empty newpath", raw_linkat(AT_FDCWD, "src", AT_FDCWD, "", 0));
    show("linkat bad flags 0x8000", raw_linkat(AT_FDCWD, "src", AT_FDCWD, "dst6", 0x8000));
    show("linkat bad flags + missing src", raw_linkat(AT_FDCWD, "nosuch", AT_FDCWD, "dst7", 0x8000));
    show("linkat bad flags + null oldpath", raw_linkat(AT_FDCWD, NULL, AT_FDCWD, "dst8", 0x8000));
    show("linkat bad dirfd 9999", raw_linkat(9999, "src", AT_FDCWD, "dst9", 0));
    show("linkat dirfd is a file", raw_linkat(f, "src", AT_FDCWD, "dst10", 0));
    int dfd = open("sub", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) { perror("open sub"); return 1; }
    show("linkat via dirfd", raw_linkat(dfd, "sfile", dfd, "slink2", 0));
    exists("sub/slink2", "sub/slink2");
    show("linkat AT_EMPTY_PATH on file fd", raw_linkat(f, "", AT_FDCWD, "dst11", 0x1000));
    exists("dst11", "dst11");
    show("linkat empty path on fd, no flag", raw_linkat(f, "", AT_FDCWD, "dst12", 0));
    int p[2]; if (pipe(p) != 0) { perror("pipe"); return 1; }
    show("linkat AT_EMPTY_PATH on pipe fd", raw_linkat(p[0], "", AT_FDCWD, "dst13", 0x1000));
    show("linkat symlink nofollow", raw_linkat(AT_FDCWD, "slink", AT_FDCWD, "dst14", 0));
    exists("dst14", "dst14 (lstat)");
    show("linkat symlink AT_SYMLINK_FOLLOW", raw_linkat(AT_FDCWD, "slink", AT_FDCWD, "dst15", 0x400));
    exists("dst15", "dst15 (lstat)"); exists("src", "src after follow");
    show("linkat flags 0x200", raw_linkat(AT_FDCWD, "src", AT_FDCWD, "dst16", 0x200));
    show("linkat missing source in subdir path", raw_linkat(AT_FDCWD, "sub/nosuch", AT_FDCWD, "dst17", 0));
    show("linkat through-file source path", raw_linkat(AT_FDCWD, "src/x", AT_FDCWD, "dst18", 0));
    return 0;
}
