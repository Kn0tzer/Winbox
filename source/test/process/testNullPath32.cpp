/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include <cstring>
#include <stdlib.h>

#ifdef __TEST
#ifndef BOXEDWINE_GUEST_X64

#include "kerror.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

// FD-132 (host-measured, test-results/fd132/hostref-fd132.c): a null
// path pointer answers -EFAULT on the host at the path calls, but
// KMemory::readString returns the empty string for a null address,
// so on the 32-bit side a null path was indistinguishable from an
// empty one and took FD-131's -ENOENT route. The measured
// exceptions: fstatat64/statx treat NULL with AT_EMPTY_PATH as the
// descriptor form (the string they receive is already ""). The
// FD-132-era claim that utimensat answers -EINVAL for a null path
// in every shape was a libc-wrapper measurement, not a kernel one:
// glibc's utimensat wrapper refuses the null path in userspace
// without issuing the call. FD-136 re-measured at the raw syscall
// level (test-results/fd136-138/hostref-fd136b.c); the per-shape
// matrix is pinned in the utimensat block below.
// (i386 linkat has no helper at all -- a nullptr dispatch entry
// answering -ENOSYS for every input -- so it is not part of this
// erasure and is not pinned here.)
void testNullPath32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    char dirTemplate[] = "/tmp/bw64fd132-32-XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("FD-132 32-bit guest test could not create its fixture dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    auto createFile = [&](BString path) -> bool {
        KFileDescriptorPtr desc;
        if (process->openFile(B("/"), path, K_O_RDWR | K_O_CREAT, desc) != 0 || !desc) return false;
        process->close(desc->handle);
        return true;
    };
    if (process->mkdir(B("/d")) != 0 || !createFile(B("/a.txt")) ||
        process->symlink(B("a.txt"), B("/ln")) != 0 || process->chdir(B("/")) != 0) {
        testFail("failed to stage the FD-132 32-bit fixture");
        return;
    }
    KFileDescriptorPtr dirDesc, fileDesc;
    if (process->openFile(B("/"), B("/"), K_O_RDONLY, dirDesc) != 0 || !dirDesc ||
        process->openFile(B("/"), B("/a.txt"), K_O_RDONLY, fileDesc) != 0 || !fileDesc) {
        testFail("failed to open FD-132 32-bit fixture descriptors");
        return;
    }
    const U32 dirfd = (U32)dirDesc->handle;
    const U32 filefd = (U32)fileDesc->handle;
    const S32 ifd = (S32)process->inotify_init1(0);
    if (ifd < 0) {
        testFail("FD-132 32-bit fixture could not create an inotify instance");
        return;
    }

    constexpr U32 atFdcwd = (U32)-100;
    constexpr U32 atEmptyPath = 0x1000;
    const U32 aTxtAt = TEST_HEAP_ADDRESS + 0x400;
    const U32 emptyAt = TEST_HEAP_ADDRESS + 0x480;
    const U32 xAt = TEST_HEAP_ADDRESS + 0x500;
    const U32 tgtAt = TEST_HEAP_ADDRESS + 0x580;
    const U32 nameAt = TEST_HEAP_ADDRESS + 0x5C0;
    const U32 argvAt = TEST_HEAP_ADDRESS + 0x600;
    const U32 envpAt = TEST_HEAP_ADDRESS + 0x680;
    const U32 bufAt = TEST_HEAP_ADDRESS + 0x800;
    const U32 rlBufAt = TEST_HEAP_ADDRESS + 0xC00;
    const U32 timesAt = TEST_HEAP_ADDRESS + 0xE00;
    const U32 times64At = TEST_HEAP_ADDRESS + 0xE80;
    auto putStr = [&](U32 at, const char* s) {
        context.memory->memcpy(at, s, (U32)strlen(s) + 1);
    };
    // testNewInstruction() resets the whole test heap, so the staged
    // strings are rewritten inside invoke() after the code emission.
    auto stageStrings = [&]() {
        putStr(aTxtAt, "/a.txt");
        putStr(emptyAt, "");
        putStr(xAt, "x");
        putStr(tgtAt, "tgt");
        putStr(nameAt, "user.x");
        U32 argv[2] = {tgtAt, 0};
        U32 envp[1] = {0};
        context.memory->memcpy(argvAt, argv, sizeof(argv));
        context.memory->memcpy(envpAt, envp, sizeof(envp));
        U32 timesLegacy[4] = {1500000000, 123000000, 1600000000, 456000000};
        context.memory->memcpy(timesAt, timesLegacy, sizeof(timesLegacy));
        U32 times64[8] = {1500000000, 0, 123000000, 0, 1600000000, 0, 456000000, 0};
        context.memory->memcpy(times64At, times64, sizeof(times64));
    };

    auto invoke = [&](U32 nr, U32 a1, U32 a2, U32 a3, U32 a4 = 0, U32 a5 = 0, U32 a6 = 0) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(nr); // mov eax, nr
        testPushCode8(0xCD); testPushCode8(0x80); // int 0x80
        stageStrings();
        context.cpu->reg[3].u32 = a1; // ebx
        context.cpu->reg[1].u32 = a2; // ecx
        context.cpu->reg[2].u32 = a3; // edx
        context.cpu->reg[6].u32 = a4; // esi
        context.cpu->reg[7].u32 = a5; // edi
        context.cpu->reg[5].u32 = a6; // ebp
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    auto expectEq = [&](const char* what, S32 got, S32 want) {
        if (got != want) {
            testFail("%s returned %d (expected %d)", what, got, want);
        }
    };

    // Controls: shapes already at the host's answer on the base.
    expectEq("open empty stays -ENOENT", invoke(5, emptyAt, 0, 0), -K_ENOENT);
    expectEq("access real path", invoke(33, aTxtAt, 0, 0), 0);
    expectEq("readlinkat null (already guarded)", invoke(305, atFdcwd, 0, rlBufAt, 256), -K_EFAULT);
    expectEq("fstatat64 null AT_EMPTY_PATH fd form", invoke(300, filefd, 0, bufAt, atEmptyPath), 0);
    expectEq("statx null AT_EMPTY_PATH fd form", invoke(383, filefd, 0, atEmptyPath, 0x7ff, bufAt), 0);
    expectEq("faccessat2 empty AT_EMPTY_PATH cwd form", invoke(439, atFdcwd, emptyAt, 0, atEmptyPath), 0);
    expectEq("inotify_add_watch bad fd null (descriptor first)", invoke(292, 4096, 0, 0xFFF), -K_EBADF);

    // The filed shapes: a null path answers -EFAULT, on either side
    // of the two-path calls.
    expectEq("open null", invoke(5, 0, 0, 0), -K_EFAULT);
    expectEq("link null from", invoke(9, 0, xAt, 0), -K_EFAULT);
    expectEq("link null to", invoke(9, aTxtAt, 0, 0), -K_EFAULT);
    expectEq("unlink null", invoke(10, 0, 0, 0), -K_EFAULT);
    expectEq("execve null", invoke(11, 0, argvAt, envpAt), -K_EFAULT);
    expectEq("chdir null", invoke(12, 0, 0, 0), -K_EFAULT);
    expectEq("chmod null", invoke(15, 0, 0644, 0), -K_EFAULT);
    expectEq("access null", invoke(33, 0, 0, 0), -K_EFAULT);
    expectEq("rename null from", invoke(38, 0, xAt, 0), -K_EFAULT);
    expectEq("rename null to", invoke(38, aTxtAt, 0, 0), -K_EFAULT);
    expectEq("mkdir null", invoke(39, 0, 0755, 0), -K_EFAULT);
    expectEq("rmdir null", invoke(40, 0, 0, 0), -K_EFAULT);
    expectEq("symlink null target", invoke(83, 0, xAt, 0), -K_EFAULT);
    expectEq("symlink null linkpath", invoke(83, tgtAt, 0, 0), -K_EFAULT);
    expectEq("readlink null", invoke(85, 0, rlBufAt, 256), -K_EFAULT);
    expectEq("statfs null", invoke(99, 0, bufAt, 0), -K_EFAULT);
    expectEq("stat64 null", invoke(195, 0, bufAt, 0), -K_EFAULT);
    expectEq("lstat64 null", invoke(196, 0, bufAt, 0), -K_EFAULT);
    expectEq("getxattr null path", invoke(229, 0, nameAt, bufAt, 256), -K_EFAULT);
    expectEq("getxattr null name", invoke(229, aTxtAt, 0, bufAt, 256), -K_EFAULT);
    expectEq("lgetxattr null path", invoke(230, 0, nameAt, bufAt, 256), -K_EFAULT);
    expectEq("statfs64 null", invoke(268, 0, 64, bufAt), -K_EFAULT);
    expectEq("utimes null", invoke(271, 0, 0, 0), -K_EFAULT);
    expectEq("inotify_add_watch null", invoke(292, (U32)ifd, 0, 0xFFF), -K_EFAULT);
    expectEq("openat null", invoke(295, atFdcwd, 0, 0), -K_EFAULT);
    expectEq("mkdirat null", invoke(296, atFdcwd, 0, 0755), -K_EFAULT);
    expectEq("fstatat64 null no flag", invoke(300, atFdcwd, 0, bufAt, 0), -K_EFAULT);
    expectEq("unlinkat null", invoke(301, atFdcwd, 0, 0), -K_EFAULT);
    expectEq("renameat null from", invoke(302, atFdcwd, 0, atFdcwd, xAt), -K_EFAULT);
    expectEq("renameat null to", invoke(302, atFdcwd, aTxtAt, atFdcwd, 0), -K_EFAULT);
    expectEq("symlinkat null target", invoke(304, 0, atFdcwd, xAt), -K_EFAULT);
    expectEq("symlinkat null linkpath", invoke(304, tgtAt, atFdcwd, 0), -K_EFAULT);
    expectEq("fchmodat null", invoke(306, atFdcwd, 0, 0644, 0), -K_EFAULT);
    expectEq("faccessat null", invoke(307, atFdcwd, 0, 0, 0), -K_EFAULT);
    expectEq("statx null no flag", invoke(383, atFdcwd, 0, 0, 0x7ff, bufAt), -K_EFAULT);
    expectEq("faccessat2 null", invoke(439, atFdcwd, 0, 0, 0), -K_EFAULT);
    expectEq("faccessat2 null AT_EMPTY_PATH (no null fd form)", invoke(439, filefd, 0, 0, atEmptyPath), -K_EFAULT);

    // utimensat with a null path answers the raw kernel's per-shape
    // matrix (FD-136, host-measured at the raw syscall level in
    // test-results/fd136-138/hostref-fd136b.c): flag bits outside
    // AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH answer -EINVAL ahead of
    // everything; the current-directory shape faults on the name
    // fetch; any other flag with a null path answers -EINVAL; with
    // flags 0 an unopened descriptor answers -EBADF and an open one
    // is the descriptor form (futimens' shape) and succeeds.
    expectEq("utimensat null AT_FDCWD", invoke(320, atFdcwd, 0, 0, 0), -K_EFAULT);
    expectEq("utimensat null AT_FDCWD AT_EMPTY_PATH", invoke(320, atFdcwd, 0, 0, atEmptyPath), -K_EFAULT);
    expectEq("utimensat null AT_FDCWD AT_SYMLINK_NOFOLLOW", invoke(320, atFdcwd, 0, 0, 0x100), -K_EFAULT);
    expectEq("utimensat null AT_FDCWD invalid flags", invoke(320, atFdcwd, 0, 0, 0x2000), -K_EINVAL);
    expectEq("utimensat null dirfd fd form (valid times)", invoke(320, dirfd, 0, timesAt, 0), 0);
    expectEq("utimensat null filefd fd form (valid times)", invoke(320, filefd, 0, timesAt, 0), 0);
    {
        // The descriptor form applies the times: the fixture file's
        // modified time is now the staged one (1600000000 seconds;
        // FsFileNode::lastModified reports host st_mtime in ms).
        std::shared_ptr<KFile> file = std::dynamic_pointer_cast<KFile>(fileDesc->kobject);
        U64 modified = (file && file->openFile && file->openFile->node) ? file->openFile->node->lastModified() : 0;
        if (modified != 1600000000ULL * 1000) {
            testFail("utimensat null filefd fd form did not apply the times: /a.txt lastModified=%llu (expected %llu)",
                     (unsigned long long)modified, (unsigned long long)(1600000000ULL * 1000));
        }
    }
    expectEq("utimensat null dirfd fd form (null times)", invoke(320, dirfd, 0, 0, 0), 0);
    expectEq("utimensat null filefd fd form (null times)", invoke(320, filefd, 0, 0, 0), 0);
    expectEq("utimensat null AT_EMPTY_PATH fd shape", invoke(320, filefd, 0, 0, atEmptyPath), -K_EINVAL);
    expectEq("utimensat null AT_SYMLINK_NOFOLLOW fd shape", invoke(320, filefd, 0, 0, 0x100), -K_EINVAL);
    expectEq("utimensat null invalid flags fd shape", invoke(320, filefd, 0, 0, 0x2000), -K_EINVAL);
    expectEq("utimensat null unopened", invoke(320, 4096, 0, 0, 0), -K_EBADF);
    expectEq("utimensat null unopened AT_EMPTY_PATH", invoke(320, 4096, 0, 0, atEmptyPath), -K_EINVAL);
    expectEq("utimensat_time64 null AT_FDCWD", invoke(412, atFdcwd, 0, 0, 0), -K_EFAULT);
    expectEq("utimensat_time64 null AT_FDCWD AT_EMPTY_PATH", invoke(412, atFdcwd, 0, 0, atEmptyPath), -K_EFAULT);
    expectEq("utimensat_time64 null filefd fd form (valid times)", invoke(412, filefd, 0, times64At, 0), 0);
    expectEq("utimensat_time64 null filefd fd form (null times)", invoke(412, filefd, 0, 0, 0), 0);
    expectEq("utimensat_time64 null unopened", invoke(412, 4096, 0, 0, 0), -K_EBADF);

    process->close((U32)ifd);
    process->close(dirDesc->handle);
    process->close(fileDesc->handle);
}

#endif
#endif
