/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"

#ifdef __TEST
#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "kstat.h"
#include "../cpu/testCPU.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"

void testStatx64Syscall() {
    TestContext& context = testContext();
    struct RootFixture {
        std::shared_ptr<FsFileNode> previous = Fs::rootNode;
        bool installed = false;
        RootFixture() {
            if (!Fs::rootNode) {
                Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), B("/tmp"), true, true, nullptr);
                Fs::clearPathCache();
                installed = true;
            }
        }
        ~RootFixture() {
            if (installed) {
                Fs::rootNode = previous;
                Fs::clearPathCache();
            }
        }
    } rootFixture;
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x76000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("x64 statx test could not map syscall buffers");
        return;
    }
    const char path[] = "/";
    constexpr U64 pathAddress = page;
    constexpr U64 statAddress = page + 0x100;
    memory->memcpyToGuest(pathAddress, path, sizeof(path));
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 dirfd, U64 pathPtr, U64 flags, U64 mask, U64 statPtr) -> S64 {
        cpu.reg[X64_RAX].setU64(332); // __NR_statx
        cpu.reg[X64_RDI].setU64(dirfd);
        cpu.reg[X64_RSI].setU64(pathPtr);
        cpu.reg[X64_RDX].setU64(flags);
        cpu.reg[X64_R10].setU64(mask);
        cpu.reg[X64_R8].setU64(statPtr);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    constexpr U64 AT_FDCWD = (U64)-100;
    constexpr U32 STATX_TYPE = 0x0001;
    constexpr U32 STATX_MODE = 0x0002;
    constexpr U32 STATX_NLINK = 0x0004;
    constexpr U32 STATX_UID = 0x0008;
    constexpr U32 STATX_GID = 0x0010;
    constexpr U32 STATX_MTIME = 0x0040;
    constexpr U32 STATX_INO = 0x0100;
    constexpr U32 STATX_SIZE = 0x0200;
    constexpr U32 STATX_BLOCKS = 0x0400;
    constexpr U32 STATX_MNT_ID = 0x1000;
    constexpr U32 expected = STATX_TYPE | STATX_MODE | STATX_NLINK | STATX_UID | STATX_GID |
                             STATX_MTIME | STATX_INO | STATX_SIZE | STATX_BLOCKS | STATX_MNT_ID;
    S64 result = invoke(AT_FDCWD, pathAddress, 0, 0x1fff, statAddress);
    if (result != 0) {
        testFail("x64 statx failed to query the VFS root: %lld", (long long)result);
        return;
    }
    if (memory->readd(statAddress) != expected ||
        (memory->readw(statAddress + 28) & K_S_IFMT) != K__S_IFDIR ||
        memory->readq(statAddress + 32) == 0 || memory->readq(statAddress + 144) != 1) {
        testFail("x64 statx returned an inaccurate support mask or basic metadata");
    }
    if (invoke(AT_FDCWD, pathAddress, 0x400, expected, statAddress) != -K_EINVAL) {
        testFail("x64 statx did not reject an invalid flag");
    }
    if (invoke(999, 0, 0x1000, expected, statAddress) != -K_EBADF) {
        testFail("x64 statx AT_EMPTY_PATH did not reject an invalid descriptor");
    }
    constexpr U64 readOnlyPage = page + K64_PAGE_SIZE;
    if (memory->mmapAnonymousFixed(readOnlyPage, K64_PAGE_SIZE, 1) != readOnlyPage) {
        testFail("x64 statx test could not map a read-only result page");
    } else if (invoke(AT_FDCWD, pathAddress, 0, expected, readOnlyPage) != -K_EFAULT) {
        testFail("x64 statx did not reject a read-only result buffer");
    }
}

#endif
#endif
