/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include "boxedwine.h"
#include <cstring>
#include <stdio.h>
#include <stdlib.h>

#ifdef __TEST

#include "kerror.h"
#include "kstat.h"
#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

// FD-133 / FD-134 / FD-135 (filed by the FD-131 lane; host-measured
// for this lane in test-results/fd133-135/hostref-fd133-135.c and
// companions): three "answers without resolving" defects in the
// path family.
//
// FD-133: the 64-bit dispatcher's extended-attribute cases
// (188-199) were flat stubs that never resolved the path or the
// descriptor they name. The host resolves first, in this order:
// the set forms validate their flags word (-EINVAL for bits
// outside XATTR_CREATE|XATTR_REPLACE), every name-taking form
// copies the attribute name (-EFAULT for a null name, ahead of
// even the empty-path verdict), and only then is the path resolved
// / the descriptor checked -- a null path faults -EFAULT, an
// unresolvable path (an empty one, a missing one, and for the
// following forms a dangling symlink) answers -ENOENT, and a bad
// descriptor answers -EBADF. Only a resolved target reaches the
// stub answers (get -ENODATA, list 0, set/remove -ENOTSUP), which
// the filing's closing terms keep: the guest filesystem models no
// attributes.
//
// FD-135: the 64-bit statfs case answered its fabricated
// statistics for every path. The host resolves the path first:
// null -EFAULT, empty or unresolvable -ENOENT, resolvable keeps
// the fabricated answer. (The process layer's own statfs methods
// already resolve this way.)
//
// FD-134: KProcess::statx replaced its path argument with the
// descriptor's directory whenever AT_EMPTY_PATH was set, so
// statx(dirfd, "name", AT_EMPTY_PATH) stat'ed the descriptor
// instead of dirfd/name. The host stats the named path; the flag
// matters only for an empty path (raw-kernel rows in the same
// hostref: dirfd/"inner" with the flag returns the file's mode
// and size, "" with the flag returns the directory's).

#ifndef BOXEDWINE_GUEST_X64

void testStatxAtEmptyPathProcessApi() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    char dirTemplate[] = "/tmp/bw64fd134apiXXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for FD-134 process-api test");
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
    if (process->mkdir(B("/d")) != 0 || !createFile(B("/a.txt")) || !createFile(B("/d/inner")) ||
        process->chdir(B("/")) != 0) {
        testFail("failed to stage the FD-134 API fixture");
        return;
    }
    KFileDescriptorPtr dirDesc;
    if (process->openFile(B("/"), B("/d"), K_O_RDONLY, dirDesc) != 0 || !dirDesc) {
        testFail("failed to open the FD-134 API fixture directory");
        return;
    }
    const FD dirfd = dirDesc->handle;
    constexpr FD atFdcwd = (FD)-100;
    constexpr U32 atEmptyPath = 0x1000; // AT_EMPTY_PATH
    constexpr U32 bufAt = TEST_HEAP_ADDRESS + 0x600;

    auto expectEq = [&](const char* what, S32 got, S32 want) {
        if (got != want) {
            testFail("%s returned %d (expected %d)", what, got, want);
        }
    };
    auto modeOf = [&](U32 buf) -> U32 {
        return process->memory->readw(buf + 28) & K_S_IFMT; // stx_mode
    };
    auto inoOf = [&](U32 buf) -> U64 {
        return process->memory->readq(buf + 32); // stx_ino
    };

    // Reference identities, through shapes that already work on
    // the base: the named file without the flag, and the fd form.
    expectEq("statx(dirfd, inner, 0) control", (S32)process->statx(dirfd, B("inner"), 0, 0x7ff, bufAt), 0);
    if (modeOf(bufAt) != K__S_IFREG) testFail("statx control: /d/inner did not stat as a regular file");
    const U64 innerIno = inoOf(bufAt);
    expectEq("statx(dirfd, \"\", AT_EMPTY_PATH) control", (S32)process->statx(dirfd, B(""), atEmptyPath, 0x7ff, bufAt), 0);
    if (modeOf(bufAt) != K__S_IFDIR) testFail("statx control: the fd form did not stat the directory");
    const U64 dirIno = inoOf(bufAt);
    if (innerIno == dirIno) testFail("statx fixture: /d and /d/inner share an inode; cannot distinguish");

    // The filed shapes: a non-empty path under AT_EMPTY_PATH stats
    // the named path, relative to the descriptor or the working
    // directory, and an absolute path stats itself.
    expectEq("statx(dirfd, inner, AT_EMPTY_PATH)", (S32)process->statx(dirfd, B("inner"), atEmptyPath, 0x7ff, bufAt), 0);
    if (modeOf(bufAt) != K__S_IFREG || inoOf(bufAt) != innerIno) {
        testFail("statx(dirfd, inner, AT_EMPTY_PATH) stat'ed mode %04o ino %llu (expected the regular file, ino %llu)",
                 modeOf(bufAt), (unsigned long long)inoOf(bufAt), (unsigned long long)innerIno);
    }
    expectEq("statx(AT_FDCWD, d/inner, AT_EMPTY_PATH)",
             (S32)process->statx(atFdcwd, B("d/inner"), atEmptyPath, 0x7ff, bufAt), 0);
    if (modeOf(bufAt) != K__S_IFREG || inoOf(bufAt) != innerIno) {
        testFail("statx(AT_FDCWD, d/inner, AT_EMPTY_PATH) did not stat the named file");
    }
    expectEq("statx(AT_FDCWD, /a.txt, AT_EMPTY_PATH)", (S32)process->statx(atFdcwd, B("/a.txt"), atEmptyPath, 0x7ff, bufAt), 0);
    if (modeOf(bufAt) != K__S_IFREG) testFail("statx(AT_FDCWD, /a.txt, AT_EMPTY_PATH) did not stat the named file");

    // A missing named path under the flag is missing, not the
    // descriptor: on the base the substitution answered 0.
    expectEq("statx(dirfd, missing, AT_EMPTY_PATH)", (S32)process->statx(dirfd, B("missing"), atEmptyPath, 0x7ff, bufAt), -K_ENOENT);

    // FD-131 control: the empty path without the flag stays refused.
    expectEq("statx(dirfd, \"\", 0) control", (S32)process->statx(dirfd, B(""), 0, 0x7ff, bufAt), -K_ENOENT);

    process->close(dirDesc->handle);
}

#endif

#ifdef BOXEDWINE_GUEST_X64

void testXattrResolve64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    char dirTemplate[] = "/tmp/bw64fd133XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("xattr test could not create its fixture dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr created;
    if (process->openFile(B("/"), B("/a.txt"), K_O_RDWR | K_O_CREAT, created) != 0 || !created ||
        process->symlink(B("a.txt"), B("/ln")) != 0 ||
        process->symlink(B("no-such-target"), B("/dang")) != 0 ||
        process->chdir(B("/")) != 0) {
        testFail("xattr test could not stage its fixture");
        return;
    }
    process->close(created->handle);
    KFileDescriptorPtr fileDesc;
    if (process->openFile(B("/"), B("/a.txt"), K_O_RDONLY, fileDesc) != 0 || !fileDesc) {
        testFail("xattr test could not open /a.txt");
        return;
    }
    const U64 fileFd = (U64)(U32)fileDesc->handle;
    const U64 badFd = 9999;

    constexpr U64 dataPage = 0x7E000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, K64_PAGE_SIZE, 3) != dataPage) {
        testFail("xattr test could not map its fixture page");
        process->close(fileDesc->handle);
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto expectEq = [&](S64 got, S64 want, const char* label) {
        if (got != want) {
            testFail("%s: got %lld, want %lld", label, (long long)got, (long long)want);
        }
    };

    constexpr U64 fileAt    = dataPage + 0x000; // "a.txt"
    constexpr U64 absAt     = dataPage + 0x080; // "/a.txt"
    constexpr U64 missingAt = dataPage + 0x100; // "no-such"
    constexpr U64 emptyAt   = dataPage + 0x180; // ""
    constexpr U64 lnAt      = dataPage + 0x200; // "ln"
    constexpr U64 dangAt    = dataPage + 0x280; // "dang"
    constexpr U64 attrAt    = dataPage + 0x300; // "user.test"
    constexpr U64 valAt     = dataPage + 0x380; // "v"
    constexpr U64 bufAt     = dataPage + 0x400; // scratch
    memory->memcpyToGuest(fileAt, "a.txt", 6);
    memory->memcpyToGuest(absAt, "/a.txt", 7);
    memory->memcpyToGuest(missingAt, "no-such", 8);
    memory->memcpyToGuest(emptyAt, "", 1);
    memory->memcpyToGuest(lnAt, "ln", 3);
    memory->memcpyToGuest(dangAt, "dang", 5);
    memory->memcpyToGuest(attrAt, "user.test", 10);
    memory->memcpyToGuest(valAt, "v", 2);

    // getxattr (191): resolution, name order, and the kept stub.
    expectEq(invoke(191, absAt, attrAt, bufAt, 64, 0), -K_ENODATA, "getxattr(/a.txt) control");
    expectEq(invoke(191, lnAt, attrAt, bufAt, 64, 0), -K_ENODATA, "getxattr(ln -> a.txt) follows");
    expectEq(invoke(191, missingAt, attrAt, bufAt, 64, 0), -K_ENOENT, "getxattr(missing)");
    expectEq(invoke(191, emptyAt, attrAt, bufAt, 64, 0), -K_ENOENT, "getxattr(\"\")");
    expectEq(invoke(191, dangAt, attrAt, bufAt, 64, 0), -K_ENOENT, "getxattr(dangling)");
    expectEq(invoke(191, 0, attrAt, bufAt, 64, 0), -K_EFAULT, "getxattr(NULL path)");
    expectEq(invoke(191, absAt, 0, bufAt, 64, 0), -K_EFAULT, "getxattr(/a.txt, NULL name)");
    expectEq(invoke(191, missingAt, 0, bufAt, 64, 0), -K_EFAULT, "getxattr(missing, NULL name) name precedes path");

    // lgetxattr (192): the link itself is the target.
    expectEq(invoke(192, dangAt, attrAt, bufAt, 64, 0), -K_ENODATA, "lgetxattr(dangling) link resolves");
    expectEq(invoke(192, missingAt, attrAt, bufAt, 64, 0), -K_ENOENT, "lgetxattr(missing)");
    expectEq(invoke(192, 0, attrAt, bufAt, 64, 0), -K_EFAULT, "lgetxattr(NULL path)");
    expectEq(invoke(192, absAt, 0, bufAt, 64, 0), -K_EFAULT, "lgetxattr(/a.txt, NULL name)");

    // listxattr (194) / llistxattr (195).
    expectEq(invoke(194, absAt, bufAt, 64, 0, 0), 0, "listxattr(/a.txt) control");
    expectEq(invoke(194, missingAt, bufAt, 64, 0, 0), -K_ENOENT, "listxattr(missing)");
    expectEq(invoke(194, emptyAt, bufAt, 64, 0, 0), -K_ENOENT, "listxattr(\"\")");
    expectEq(invoke(194, dangAt, bufAt, 64, 0, 0), -K_ENOENT, "listxattr(dangling)");
    expectEq(invoke(194, 0, bufAt, 64, 0, 0), -K_EFAULT, "listxattr(NULL path)");
    expectEq(invoke(195, dangAt, bufAt, 64, 0, 0), 0, "llistxattr(dangling) link resolves");
    expectEq(invoke(195, missingAt, bufAt, 64, 0, 0), -K_ENOENT, "llistxattr(missing)");

    // setxattr (188): flags validation precedes the name copy,
    // which precedes path resolution (all host-measured).
    expectEq(invoke(188, absAt, attrAt, valAt, 1, 0), -K_ENOTSUP, "setxattr(/a.txt) control");
    expectEq(invoke(188, missingAt, attrAt, valAt, 1, 0), -K_ENOENT, "setxattr(missing)");
    expectEq(invoke(188, emptyAt, attrAt, valAt, 1, 0), -K_ENOENT, "setxattr(\"\")");
    expectEq(invoke(188, 0, attrAt, valAt, 1, 0), -K_EFAULT, "setxattr(NULL path)");
    expectEq(invoke(188, absAt, 0, valAt, 1, 0), -K_EFAULT, "setxattr(/a.txt, NULL name)");
    expectEq(invoke(188, missingAt, 0, valAt, 1, 0), -K_EFAULT, "setxattr(missing, NULL name)");
    expectEq(invoke(188, absAt, attrAt, valAt, 1, 0x10), -K_EINVAL, "setxattr(/a.txt, bad flags)");
    expectEq(invoke(188, missingAt, attrAt, valAt, 1, 0x10), -K_EINVAL, "setxattr(missing, bad flags) flags precede path");
    expectEq(invoke(188, absAt, 0, valAt, 1, 0x10), -K_EINVAL, "setxattr(/a.txt, NULL name, bad flags) flags precede name");

    // lsetxattr (189).
    expectEq(invoke(189, absAt, attrAt, valAt, 1, 0), -K_ENOTSUP, "lsetxattr(/a.txt) control");
    expectEq(invoke(189, missingAt, attrAt, valAt, 1, 0), -K_ENOENT, "lsetxattr(missing)");

    // removexattr (197) / lremovexattr (198).
    expectEq(invoke(197, absAt, attrAt, 0, 0, 0), -K_ENOTSUP, "removexattr(/a.txt) control");
    expectEq(invoke(197, missingAt, attrAt, 0, 0, 0), -K_ENOENT, "removexattr(missing)");
    expectEq(invoke(197, emptyAt, attrAt, 0, 0, 0), -K_ENOENT, "removexattr(\"\")");
    expectEq(invoke(197, 0, attrAt, 0, 0, 0), -K_EFAULT, "removexattr(NULL path)");
    expectEq(invoke(197, absAt, 0, 0, 0, 0), -K_EFAULT, "removexattr(/a.txt, NULL name)");
    expectEq(invoke(198, missingAt, attrAt, 0, 0, 0), -K_ENOENT, "lremovexattr(missing)");

    // The descriptor forms: the name copy precedes the descriptor
    // check; the set form's flags precede both.
    expectEq(invoke(193, fileFd, attrAt, bufAt, 64, 0), -K_ENODATA, "fgetxattr(fd) control");
    expectEq(invoke(193, badFd, attrAt, bufAt, 64, 0), -K_EBADF, "fgetxattr(bad fd)");
    expectEq(invoke(193, fileFd, 0, bufAt, 64, 0), -K_EFAULT, "fgetxattr(fd, NULL name)");
    expectEq(invoke(193, badFd, 0, bufAt, 64, 0), -K_EFAULT, "fgetxattr(bad fd, NULL name) name precedes fd");
    expectEq(invoke(196, fileFd, bufAt, 64, 0, 0), 0, "flistxattr(fd) control");
    expectEq(invoke(196, badFd, bufAt, 64, 0, 0), -K_EBADF, "flistxattr(bad fd)");
    expectEq(invoke(190, fileFd, attrAt, valAt, 1, 0), -K_ENOTSUP, "fsetxattr(fd) control");
    expectEq(invoke(190, badFd, attrAt, valAt, 1, 0), -K_EBADF, "fsetxattr(bad fd)");
    expectEq(invoke(190, badFd, attrAt, valAt, 1, 0x10), -K_EINVAL, "fsetxattr(bad fd, bad flags) flags precede fd");
    expectEq(invoke(190, fileFd, 0, valAt, 1, 0), -K_EFAULT, "fsetxattr(fd, NULL name)");
    expectEq(invoke(199, fileFd, attrAt, 0, 0, 0), -K_ENOTSUP, "fremovexattr(fd) control");
    expectEq(invoke(199, badFd, attrAt, 0, 0, 0), -K_EBADF, "fremovexattr(bad fd)");

    process->close(fileDesc->handle);
}

void testStatfsResolve64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    char dirTemplate[] = "/tmp/bw64fd135XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("statfs test could not create its fixture dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr created;
    if (process->openFile(B("/"), B("/a.txt"), K_O_RDWR | K_O_CREAT, created) != 0 || !created ||
        process->symlink(B("a.txt"), B("/ln")) != 0 ||
        process->symlink(B("no-such-target"), B("/dang")) != 0 ||
        process->chdir(B("/")) != 0) {
        testFail("statfs test could not stage its fixture");
        return;
    }
    process->close(created->handle);

    constexpr U64 dataPage = 0x7E000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, K64_PAGE_SIZE, 3) != dataPage) {
        testFail("statfs test could not map its fixture page");
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 nr, U64 a1, U64 a2) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto expectEq = [&](S64 got, S64 want, const char* label) {
        if (got != want) {
            testFail("%s: got %lld, want %lld", label, (long long)got, (long long)want);
        }
    };

    constexpr U64 absAt     = dataPage + 0x000; // "/a.txt"
    constexpr U64 rootAt    = dataPage + 0x080; // "/"
    constexpr U64 missingAt = dataPage + 0x100; // "no-such"
    constexpr U64 emptyAt   = dataPage + 0x180; // ""
    constexpr U64 lnAt      = dataPage + 0x200; // "ln"
    constexpr U64 dangAt    = dataPage + 0x280; // "dang"
    constexpr U64 childAt   = dataPage + 0x300; // "a.txt/child"
    constexpr U64 bufAt     = dataPage + 0x400; // struct statfs out
    memory->memcpyToGuest(absAt, "/a.txt", 7);
    memory->memcpyToGuest(rootAt, "/", 2);
    memory->memcpyToGuest(missingAt, "no-such", 8);
    memory->memcpyToGuest(emptyAt, "", 1);
    memory->memcpyToGuest(lnAt, "ln", 3);
    memory->memcpyToGuest(dangAt, "dang", 5);
    memory->memcpyToGuest(childAt, "a.txt/child", 12);

    // statfs (137): resolvable paths keep the fabricated answer
    // (f_type is the tmpfs magic the stub writes); the rest now
    // resolve first, as the host does.
    expectEq(invoke(137, absAt, bufAt), 0, "statfs(/a.txt) control");
    if (memory->readq(bufAt) != 0x01021994) testFail("statfs(/a.txt) did not write the fabricated f_type");
    expectEq(invoke(137, rootAt, bufAt), 0, "statfs(/) control");
    expectEq(invoke(137, lnAt, bufAt), 0, "statfs(ln -> a.txt) follows");
    expectEq(invoke(137, missingAt, bufAt), -K_ENOENT, "statfs(missing)");
    expectEq(invoke(137, emptyAt, bufAt), -K_ENOENT, "statfs(\"\")");
    expectEq(invoke(137, dangAt, bufAt), -K_ENOENT, "statfs(dangling)");
    expectEq(invoke(137, 0, bufAt), -K_EFAULT, "statfs(NULL)");

    // Printed observation, not pinned: a path through a regular
    // file answers -ENOTDIR on the host; the shared resolver
    // reports no node, so the fixed case answers -ENOENT. Recorded
    // in the lane record as a resolver-level observation.
    printf("FD-135 observation: statfs(\"a.txt/child\") -> %lld (host: -ENOTDIR)\n",
           (long long)invoke(137, childAt, bufAt));
}

#endif

#endif
