// hostref-linkat.c — host reference for linkat, measured through
// the ia32 number (303, int 0x80: the compat entry a 32-bit caller
// reaches) with native 64-bit spot checks at the end. Paths live
// below 4 GiB (MAP_32BIT). Result observation (stat/lstat) is
// host-side and not part of the measurement.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stdint.h>
#ifndef MAP_32BIT
#define MAP_32BIT 0x40
#endif

#define FDCWD32 0xFFFFFF9CU /* AT_FDCWD (-100) as a 32-bit argument */

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
static void show(const char* name, long r) {
    if (r < 0) printf("%-52s -> %ld (errno %ld: %s)\n", name, r, -r, strerror((int)-r));
    else printf("%-52s -> %ld\n", name, r);
    fflush(stdout);
}
static char* low32str(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p == MAP_FAILED) { perror("mmap32"); exit(1); }
    memcpy(p, s, n);
    return p;
}
static uint32_t P(const char* s) { return (uint32_t)(uintptr_t)low32str(s); }
static long lat32(uint32_t olddf, const char* oldp, uint32_t newdf,
                  const char* newp, uint32_t flags) {
    return sys32(303, olddf, oldp ? P(oldp) : 0, newdf, newp ? P(newp) : 0, flags, 0);
}
static void exists(const char* path, const char* what) {
    struct stat st;
    if (lstat(path, &st) == 0)
        printf("  %s: exists mode=%o nlink=%lu\n", what, st.st_mode, (unsigned long)st.st_nlink);
    else
        printf("  %s: absent\n", what);
}
int main() {
    char dir[] = "/tmp/hostref-linkatXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    if (chdir(dir) != 0) { perror("chdir"); return 1; }
    int f = open("src", O_RDWR | O_CREAT, 0644); if (f < 0) { perror("src"); return 1; }
    if (write(f, "data", 4) != 4) { perror("write"); return 1; }
    if (mkdir("sub", 0755) != 0) { perror("mkdir sub"); return 1; }
    int sf = open("sub/sfile", O_RDWR | O_CREAT, 0644); if (sf < 0) { perror("sfile"); return 1; }
    close(sf);
    if (mkdir("srcdir", 0755) != 0) { perror("mkdir srcdir"); return 1; }
    if (symlink("src", "slink") != 0) { perror("symlink"); return 1; }

    show("ia32 linkat basic", lat32(FDCWD32, "src", FDCWD32, "dst", 0));
    exists("dst", "dst"); exists("src", "src");
    show("ia32 linkat missing source", lat32(FDCWD32, "nosuch", FDCWD32, "dst2", 0));
    show("ia32 linkat existing target", lat32(FDCWD32, "src", FDCWD32, "dst", 0));
    show("ia32 linkat directory source", lat32(FDCWD32, "srcdir", FDCWD32, "dst3", 0));
    show("ia32 linkat null oldpath", lat32(FDCWD32, NULL, FDCWD32, "dst4", 0));
    show("ia32 linkat null newpath", lat32(FDCWD32, "src", FDCWD32, NULL, 0));
    show("ia32 linkat empty oldpath", lat32(FDCWD32, "", FDCWD32, "dst5", 0));
    show("ia32 linkat empty newpath", lat32(FDCWD32, "src", FDCWD32, "", 0));
    show("ia32 linkat bad flags 0x8000", lat32(FDCWD32, "src", FDCWD32, "dst6", 0x8000));
    show("ia32 linkat bad flags + missing src", lat32(FDCWD32, "nosuch", FDCWD32, "dst7", 0x8000));
    show("ia32 linkat bad flags + null oldpath", lat32(FDCWD32, NULL, FDCWD32, "dst8", 0x8000));
    show("ia32 linkat bad dirfd 9999", lat32(9999, "src", FDCWD32, "dst9", 0));
    show("ia32 linkat dirfd is a file", lat32(f, "src", FDCWD32, "dst10", 0));
    int dfd = open("sub", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) { perror("open sub"); return 1; }
    show("ia32 linkat via dirfd", lat32(dfd, "sfile", dfd, "slink2", 0));
    exists("sub/slink2", "sub/slink2");
    show("ia32 linkat AT_EMPTY_PATH on file fd", lat32(f, "", FDCWD32, "dst11", 0x1000));
    exists("dst11", "dst11");
    show("ia32 linkat empty path on fd, no flag", lat32(f, "", FDCWD32, "dst12", 0));
    int p[2]; if (pipe(p) != 0) { perror("pipe"); return 1; }
    show("ia32 linkat AT_EMPTY_PATH on pipe fd", lat32(p[0], "", FDCWD32, "dst13", 0x1000));
    show("ia32 linkat symlink nofollow", lat32(FDCWD32, "slink", FDCWD32, "dst14", 0));
    exists("dst14", "dst14 (lstat)");
    show("ia32 linkat symlink AT_SYMLINK_FOLLOW", lat32(FDCWD32, "slink", FDCWD32, "dst15", 0x400));
    exists("dst15", "dst15 (lstat)"); exists("src", "src after follow");
    show("ia32 linkat flags 0x200", lat32(FDCWD32, "src", FDCWD32, "dst16", 0x200));

    // Native 64-bit spot checks (same kernel entry, wide args).
    errno = 0;
    long r = syscall(SYS_linkat, AT_FDCWD, "src", AT_FDCWD, "dst64", 0);
    if (r < 0) printf("%-52s -> -1 errno=%d (%s)\n", "x64 linkat basic", errno, strerror(errno));
    else printf("%-52s -> %ld\n", "x64 linkat basic", r);
    errno = 0;
    r = syscall(SYS_linkat, AT_FDCWD, NULL, AT_FDCWD, "dst65", 0);
    if (r < 0) printf("%-52s -> -1 errno=%d (%s)\n", "x64 linkat null oldpath", errno, strerror(errno));
    else printf("%-52s -> %ld\n", "x64 linkat null oldpath", r);
    errno = 0;
    r = syscall(SYS_linkat, AT_FDCWD, "src", AT_FDCWD, "dst66", 0x8000);
    if (r < 0) printf("%-52s -> -1 errno=%d (%s)\n", "x64 linkat bad flags 0x8000", errno, strerror(errno));
    else printf("%-52s -> %ld\n", "x64 linkat bad flags 0x8000", r);
    return 0;
}
