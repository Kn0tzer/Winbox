#include "boxedwine.h"
#include <cstring>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifdef __TEST
#ifndef BOXEDWINE_GUEST_X64

#include "kerror.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

// Main-suite companions to testNotDir64.cpp (same filings, measured
// in test-results/fd139-152/hostref-fd139-152*.c):
//   - utimensat flag bits outside AT_SYMLINK_NOFOLLOW |
//     AT_EMPTY_PATH answer -EINVAL on the host for non-null paths
//     too, on both the 32-bit utimensat and utimensat_time64 forms;
//     the emulator's real call consulted only the two documented
//     bits and set the times anyway.
//   - a path whose prefix component is not a directory answers
//     -ENOTDIR on the host at the path calls; the shared resolver
//     reported no node and every call routed through it answered
//     the bare -ENOENT.
// Fixture tree: /a.txt (file), /d (directory) with /d/inner (file),
// /ln -> a.txt, /ld -> d, /c.txt (file, for the rename control).

namespace {

struct RootSwap32 {
    std::shared_ptr<FsFileNode> previousRoot;
    std::string dir;
    bool ok = false;

    RootSwap32() {
        char dirTemplate[] = "/tmp/bw64fd152apiXXXXXX";
        char* d = mkdtemp(dirTemplate);
        if (!d) return;
        dir = d;
        previousRoot = Fs::rootNode;
        Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir.c_str()), true, true, nullptr);
        Fs::clearPathCache();
        ok = true;
    }
    ~RootSwap32() {
        if (previousRoot) {
            Fs::rootNode = previousRoot;
            Fs::clearPathCache();
        }
    }
};

bool stageTree32(KProcessPtr process, FD& dirFdOut) {
    KFileDescriptorPtr dirDesc;
    auto createFile = [&](BString path) -> bool {
        KFileDescriptorPtr desc;
        if (process->openFile(B("/"), path, K_O_RDWR | K_O_CREAT, desc) != 0 || !desc) return false;
        process->close(desc->handle);
        return true;
    };
    if (process->mkdir(B("/d")) != 0 ||
        !createFile(B("/a.txt")) || !createFile(B("/d/inner")) || !createFile(B("/c.txt")) ||
        process->symlink(B("a.txt"), B("/ln")) != 0 ||
        process->symlink(B("d"), B("/ld")) != 0 ||
        process->chdir(B("/")) != 0) {
        return false;
    }
    if (process->openFile(B("/"), B("/d"), K_O_RDONLY, dirDesc) != 0 || !dirDesc) return false;
    dirFdOut = dirDesc->handle;
    return true;
}

} // namespace

void testUtimensatFlags32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    CPU* cpu = context.cpu;

    RootSwap32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTree32(process, dirFd)) {
        testFail("utimensat flags 32 test could not stage its fixture");
        return;
    }

    constexpr U32 aTxtAt       = TEST_HEAP_ADDRESS + 0x000; // "/a.txt"
    constexpr U32 missingAt    = TEST_HEAP_ADDRESS + 0x040; // "/no-such"
    constexpr U32 emptyAt      = TEST_HEAP_ADDRESS + 0x080; // ""
    constexpr U32 innerAt      = TEST_HEAP_ADDRESS + 0x0C0; // "inner"
    constexpr U32 childAt      = TEST_HEAP_ADDRESS + 0x100; // "/a.txt/child"
    constexpr U32 timesAt      = TEST_HEAP_ADDRESS + 0x140; // two 32-bit timespecs, sec 1500000000
    constexpr U32 times2At     = TEST_HEAP_ADDRESS + 0x180; // two 32-bit timespecs, sec 1600000000
    constexpr U32 times64At    = TEST_HEAP_ADDRESS + 0x1C0; // two 64-bit timespecs, sec 1500000000
    constexpr U32 atFdcwd = (U32)-100;

    // testNewInstruction() resets the whole test heap, so the
    // staged strings are rewritten inside invoke() after the code
    // emission (the testNullPath32Syscalls pattern).
    auto stage = [&]() {
        context.memory->memcpy(aTxtAt, "/a.txt", 7);
        context.memory->memcpy(missingAt, "/no-such", 9);
        context.memory->memcpy(emptyAt, "", 1);
        context.memory->memcpy(innerAt, "inner", 6);
        context.memory->memcpy(childAt, "/a.txt/child", 13);
        U8 t32[16] = {};
        U32 sec = 1500000000;
        memcpy(t32, &sec, 4); memcpy(t32 + 8, &sec, 4);
        context.memory->memcpy(timesAt, t32, sizeof(t32));
        U8 t32b[16] = {};
        U32 sec2 = 1600000000;
        memcpy(t32b, &sec2, 4); memcpy(t32b + 8, &sec2, 4);
        context.memory->memcpy(times2At, t32b, sizeof(t32b));
        U8 t64[32] = {};
        U64 sec64 = 1500000000ULL;
        memcpy(t64, &sec64, 8); memcpy(t64 + 16, &sec64, 8);
        context.memory->memcpy(times64At, t64, sizeof(t64));
    };
    auto invoke = [&](U32 nr, U32 a1, U32 a2, U32 a3, U32 a4) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(nr); // mov eax, nr
        testPushCode8(0xCD); testPushCode8(0x80); // int 0x80
        stage();
        cpu->reg[3].u32 = a1; // ebx
        cpu->reg[1].u32 = a2; // ecx
        cpu->reg[2].u32 = a3; // edx
        cpu->reg[6].u32 = a4; // esi
        testRunCPU();
        return (S32)cpu->reg[0].u32;
    };
    auto expectEq = [&](const char* what, S32 got, S32 want) {
        if (got != want) testFail("%s returned %d (expected %d)", what, got, want);
    };
    auto nativeMtime = [&]() -> long {
        struct stat st;
        std::string p = root.dir + "/a.txt";
        if (::stat(p.c_str(), &st) != 0) return -1;
        return (long)st.st_mtime;
    };

    // Controls: the documented bits in any combination.
    expectEq("utimensat(/a.txt, 0)", invoke(320, atFdcwd, aTxtAt, timesAt, 0), 0);
    expectEq("utimensat(/a.txt, AT_SYMLINK_NOFOLLOW)", invoke(320, atFdcwd, aTxtAt, timesAt, 0x100), 0);
    expectEq("utimensat(/a.txt, AT_EMPTY_PATH)", invoke(320, atFdcwd, aTxtAt, timesAt, 0x1000), 0);
    expectEq("utimensat(/a.txt, both bits)", invoke(320, atFdcwd, aTxtAt, timesAt, 0x1100), 0);
    // The valid call above set the file's times (sec 1500000000).
    if (nativeMtime() != 1500000000L) testFail("utimensat control did not set the file time (mtime %ld)", nativeMtime());

    // Any other bit answers -EINVAL and the times stay untouched.
    expectEq("utimensat(/a.txt, 0x2000)", invoke(320, atFdcwd, aTxtAt, times2At, 0x2000), -K_EINVAL);
    if (nativeMtime() != 1500000000L) testFail("a refused utimensat changed the file time (mtime %ld)", nativeMtime());
    expectEq("utimensat(/a.txt, 0x1)", invoke(320, atFdcwd, aTxtAt, timesAt, 0x1), -K_EINVAL);
    expectEq("utimensat(/a.txt, all bits)", invoke(320, atFdcwd, aTxtAt, timesAt, 0xFFFFFFFF), -K_EINVAL);
    expectEq("utimensat(missing, 0x2000): flags precede the name fetch", invoke(320, atFdcwd, missingAt, timesAt, 0x2000), -K_EINVAL);
    expectEq("utimensat(\"\", 0x2000): flags precede the empty-path answer", invoke(320, atFdcwd, emptyAt, timesAt, 0x2000), -K_EINVAL);
    expectEq("utimensat(dirfd, inner, 0x2000)", invoke(320, dirFd, innerAt, timesAt, 0x2000), -K_EINVAL);
    expectEq("utimensat(dirfd, inner, 0) control", invoke(320, dirFd, innerAt, timesAt, 0), 0);

    // The through-a-file shape at the same call: flags still first,
    // then the resolution verdict.
    expectEq("utimensat(/a.txt/child, 0x2000)", invoke(320, atFdcwd, childAt, timesAt, 0x2000), -K_EINVAL);
    expectEq("utimensat(/a.txt/child, 0)", invoke(320, atFdcwd, childAt, timesAt, 0), -K_ENOTDIR);

    // The time64 form validates the same way.
    expectEq("utimensat_time64(/a.txt, 0) control", invoke(412, atFdcwd, aTxtAt, times64At, 0), 0);
    expectEq("utimensat_time64(/a.txt, 0x2000)", invoke(412, atFdcwd, aTxtAt, times64At, 0x2000), -K_EINVAL);
}

void testNotDir32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    CPU* cpu = context.cpu;

    RootSwap32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTree32(process, dirFd)) {
        testFail("ENOTDIR 32 test could not stage its fixture");
        return;
    }
    (void)dirFd;

    constexpr U32 aTxtAt       = TEST_HEAP_ADDRESS + 0x000; // "/a.txt"
    constexpr U32 childAt      = TEST_HEAP_ADDRESS + 0x040; // "a.txt/child"
    constexpr U32 missingChild = TEST_HEAP_ADDRESS + 0x080; // "missing/child"
    constexpr U32 xAt          = TEST_HEAP_ADDRESS + 0x0C0; // "x"
    constexpr U32 tgtAt        = TEST_HEAP_ADDRESS + 0x100; // "t"
    constexpr U32 dosAt        = TEST_HEAP_ADDRESS + 0x140; // "user.DOSATTRIB"
    constexpr U32 bufAt        = TEST_HEAP_ADDRESS + 0x180; // out buffers

    auto stage = [&]() {
        context.memory->memcpy(aTxtAt, "/a.txt", 7);
        context.memory->memcpy(childAt, "a.txt/child", 12);
        context.memory->memcpy(missingChild, "missing/child", 14);
        context.memory->memcpy(xAt, "x", 2);
        context.memory->memcpy(tgtAt, "t", 2);
        context.memory->memcpy(dosAt, "user.DOSATTRIB", 15);
    };
    auto invoke = [&](U32 nr, U32 a1, U32 a2, U32 a3, U32 a4) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(nr); // mov eax, nr
        testPushCode8(0xCD); testPushCode8(0x80); // int 0x80
        stage();
        cpu->reg[3].u32 = a1; // ebx
        cpu->reg[1].u32 = a2; // ecx
        cpu->reg[2].u32 = a3; // edx
        cpu->reg[6].u32 = a4; // esi
        testRunCPU();
        return (S32)cpu->reg[0].u32;
    };
    auto expectEq = [&](const char* what, S32 got, S32 want) {
        if (got != want) testFail("%s returned %d (expected %d)", what, got, want);
    };

    expectEq("statfs(a.txt/child)", invoke(99, childAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq("statfs(/a.txt) control", invoke(99, aTxtAt, bufAt, 0, 0), 0);
    expectEq("stat64(a.txt/child)", invoke(195, childAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq("stat64(missing/child) control", invoke(195, missingChild, bufAt, 0, 0), -K_ENOENT);
    expectEq("access(a.txt/child)", invoke(33, childAt, 0, 0, 0), -K_ENOTDIR);
    expectEq("chmod(a.txt/child)", invoke(15, childAt, 0644, 0, 0), -K_ENOTDIR);
    expectEq("mkdir(a.txt/child)", invoke(39, childAt, 0755, 0, 0), -K_ENOTDIR);
    expectEq("rmdir(a.txt/child)", invoke(40, childAt, 0, 0, 0), -K_ENOTDIR);
    expectEq("unlink(a.txt/child)", invoke(10, childAt, 0, 0, 0), -K_ENOTDIR);
    expectEq("chdir(a.txt/child)", invoke(12, childAt, 0, 0, 0), -K_ENOTDIR);
    expectEq("rename(/a.txt, a.txt/child)", invoke(38, aTxtAt, childAt, 0, 0), -K_ENOTDIR);
    expectEq("rename(a.txt/child, x)", invoke(38, childAt, xAt, 0, 0), -K_ENOTDIR);
    expectEq("link(/a.txt, a.txt/child)", invoke(9, aTxtAt, childAt, 0, 0), -K_ENOTDIR);
    expectEq("symlink(t, a.txt/child)", invoke(83, tgtAt, childAt, 0, 0), -K_ENOTDIR);
    expectEq("readlink(a.txt/child)", invoke(85, childAt, bufAt, 256, 0), -K_ENOTDIR);

    // The create form of open must not fabricate a child under the
    // file either (kept late: on the base it pollutes the fixture).
    expectEq("open(a.txt/child, O_RDONLY)", invoke(5, childAt, 0, 0, 0), -K_ENOTDIR);
    expectEq("open(a.txt/child, O_WRONLY|O_CREAT)", invoke(5, childAt, 0x41, 0644, 0), -K_ENOTDIR);

    // The 32-bit xattr cases resolve only for the DOSATTRIB name;
    // inside that branch the through-file shape answers -ENOTDIR
    // like everywhere else. (The non-resolution for other names is
    // filed separately.)
    expectEq("getxattr(a.txt/child, user.DOSATTRIB)", invoke(229, childAt, dosAt, bufAt, 64), -K_ENOTDIR);
    expectEq("getxattr(/a.txt, user.DOSATTRIB) control", invoke(229, aTxtAt, dosAt, bufAt, 64), -K_ENODATA);

    // inotify_add_watch (292) on an inotify descriptor (332).
    S32 ifd = invoke(332, 0, 0, 0, 0);
    if (ifd < 0) {
        testFail("inotify_init1 returned %d", ifd);
    } else {
        expectEq("inotify_add_watch(a.txt/child)", invoke(292, (U32)ifd, childAt, 0xfff, 0), -K_ENOTDIR);
        if (invoke(292, (U32)ifd, aTxtAt, 0xfff, 0) < 0) testFail("inotify_add_watch(/a.txt) control failed");
    }
}

void testNotDirProcessApi() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    RootSwap32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTree32(process, dirFd)) {
        testFail("ENOTDIR process-api test could not stage its fixture");
        return;
    }
    constexpr FD atFdcwd = (FD)-100;
    constexpr U32 bufAt = TEST_HEAP_ADDRESS + 0x600;

    auto expectEq = [&](const char* what, S32 got, S32 want) {
        if (got != want) testFail("%s returned %d (expected %d)", what, got, want);
    };

    expectEq("stat64(a.txt/child)", (S32)process->stat64(B("a.txt/child"), bufAt), -K_ENOTDIR);
    expectEq("stat64(missing/child) control", (S32)process->stat64(B("missing/child"), bufAt), -K_ENOENT);
    expectEq("stat64(a.txt) control", (S32)process->stat64(B("a.txt"), bufAt), 0);
    expectEq("stat64(ln/child) through a symlink to a file", (S32)process->stat64(B("ln/child"), bufAt), -K_ENOTDIR);
    expectEq("stat64(ld/inner) through a symlink to a dir control", (S32)process->stat64(B("ld/inner"), bufAt), 0);
    expectEq("lstat64(a.txt/child)", (S32)process->lstat64(B("a.txt/child"), bufAt), -K_ENOTDIR);
    expectEq("statfs(a.txt/child)", (S32)process->statfs(B("a.txt/child"), bufAt), -K_ENOTDIR);
    expectEq("statfs64(a.txt/child)", (S32)process->statfs64(B("a.txt/child"), bufAt), -K_ENOTDIR);
    expectEq("statfs(a.txt) control", (S32)process->statfs(B("a.txt"), bufAt), 0);
    expectEq("access(a.txt/child)", (S32)process->access(B("a.txt/child"), 0), -K_ENOTDIR);
    expectEq("access(a.txt) control", (S32)process->access(B("a.txt"), 0), 0);
    expectEq("chmod(a.txt/child)", (S32)process->chmod(B("a.txt/child"), 0644), -K_ENOTDIR);
    expectEq("fchmodat(a.txt/child)", (S32)process->fchmodat(atFdcwd, B("a.txt/child"), 0644, 0), -K_ENOTDIR);
    expectEq("chdir(a.txt/child)", (S32)process->chdir(B("a.txt/child")), -K_ENOTDIR);
    expectEq("unlinkFile(a.txt/child)", (S32)process->unlinkFile(B("a.txt/child")), -K_ENOTDIR);
    expectEq("rmdir(a.txt/child)", (S32)process->rmdir(B("a.txt/child")), -K_ENOTDIR);
    expectEq("mkdir(a.txt/child)", (S32)process->mkdir(B("a.txt/child")), -K_ENOTDIR);
    expectEq("mkdir(a.txt/deep/child)", (S32)process->mkdir(B("a.txt/deep/child")), -K_ENOTDIR);
    expectEq("mkdir(missing/child) control", (S32)process->mkdir(B("missing/child")), -K_ENOENT);
    expectEq("mkdirat(dirfd, inner/child)", (S32)process->mkdirat(dirFd, B("inner/child"), 0755), -K_ENOTDIR);
    expectEq("rename(a.txt, a.txt/child)", (S32)process->rename(B("a.txt"), B("a.txt/child")), -K_ENOTDIR);
    expectEq("rename(a.txt/child, x)", (S32)process->rename(B("a.txt/child"), B("x")), -K_ENOTDIR);
    expectEq("link(a.txt, a.txt/child)", (S32)process->link(B("a.txt"), B("a.txt/child")), -K_ENOTDIR);
    expectEq("link(a.txt/child, x)", (S32)process->link(B("a.txt/child"), B("x")), -K_ENOTDIR);
    // The destination carries a parent component on purpose: a
    // bare filename destination answers -ENOENT on this tree for
    // an unrelated reason (its parent-path computation), filed
    // separately.
    expectEq("link(a.txt, d/hl.txt) control", (S32)process->link(B("a.txt"), B("d/hl.txt")), 0);
    expectEq("symlink(t, a.txt/child)", (S32)process->symlink(B("t"), B("a.txt/child")), -K_ENOTDIR);
    expectEq("symlink(a.txt, /sl) control", (S32)process->symlink(B("a.txt"), B("/sl")), 0);
    expectEq("readlink(a.txt/child)", (S32)process->readlink(B("a.txt/child"), bufAt, 256), -K_ENOTDIR);
    expectEq("utimes(a.txt/child)", (S32)process->utimes(B("a.txt/child"), 0), -K_ENOTDIR);
    expectEq("utimesat(a.txt/child)", (S32)process->utimesat(atFdcwd, B("a.txt/child"), 0, 0, false), -K_ENOTDIR);
    expectEq("utimesat(dirfd, inner/child)", (S32)process->utimesat(dirFd, B("inner/child"), 0, 0, false), -K_ENOTDIR);
    expectEq("utimesat(dirfd, inner) control", (S32)process->utimesat(dirFd, B("inner"), 0, 0, false), 0);
    expectEq("fstatat64(dirfd, inner/child)", (S32)process->fstatat64(dirFd, B("inner/child"), bufAt, 0), -K_ENOTDIR);
    expectEq("fstatat64(a.txt/child)", (S32)process->fstatat64(atFdcwd, B("a.txt/child"), bufAt, 0), -K_ENOTDIR);
    expectEq("fstatat64(dirfd, inner) control", (S32)process->fstatat64(dirFd, B("inner"), bufAt, 0), 0);
    expectEq("statx(a.txt/child)", (S32)process->statx(atFdcwd, B("a.txt/child"), 0, 0x7ff, bufAt), -K_ENOTDIR);
    expectEq("statx(dirfd, inner/child)", (S32)process->statx(dirFd, B("inner/child"), 0, 0x7ff, bufAt), -K_ENOTDIR);
    expectEq("statx(dirfd, inner) control", (S32)process->statx(dirFd, B("inner"), 0, 0x7ff, bufAt), 0);
    expectEq("faccessat(a.txt/child)", (S32)process->faccessat(atFdcwd, B("a.txt/child"), 0, 0), -K_ENOTDIR);
    expectEq("unlinkat(a.txt/child)", (S32)process->unlinkat(atFdcwd, B("a.txt/child"), 0), -K_ENOTDIR);

    expectEq("open(a.txt/child, O_RDONLY)", (S32)process->open(B("a.txt/child"), K_O_RDONLY), -K_ENOTDIR);
    expectEq("open(a.txt/child, O_CREAT)", (S32)process->open(B("a.txt/child"), K_O_CREAT | K_O_WRONLY), -K_ENOTDIR);
    expectEq("open(missing/child) control", (S32)process->open(B("missing/child"), K_O_RDONLY), -K_ENOENT);
    expectEq("openat(dirfd, inner/child)", (S32)process->openat(dirFd, B("inner/child"), K_O_RDONLY), -K_ENOTDIR);
    U32 opened = process->open(B("a.txt"), K_O_RDONLY);
    if ((S32)opened < 0) testFail("open(a.txt) control returned %d", (S32)opened);
    else process->close((FD)opened);
    U32 opened2 = process->openat(dirFd, B("inner"), K_O_RDONLY);
    if ((S32)opened2 < 0) testFail("openat(dirfd, inner) control returned %d", (S32)opened2);
    else process->close((FD)opened2);

    // execve resolves the program path the same way.
    std::vector<BString> args = { B("a.txt/child") };
    std::vector<BString> envs;
    expectEq("execve(a.txt/child)", (S32)process->execve(context.thread, B("a.txt/child"), args, envs), -K_ENOTDIR);

    // Positive controls last: a rename that must keep working.
    expectEq("rename(c.txt, c2.txt) control", (S32)process->rename(B("c.txt"), B("c2.txt")), 0);
}

#endif
#endif
