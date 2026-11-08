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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef __TEST

#include "kerror.h"
#include "ksocket.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

// FD-193 / FD-194 / FD-195: the vectored-read length contract, the
// positioned-read-on-an-unseekable-object contract, and the 32-bit
// linkat contract, all measured at the raw syscall level on the
// host's 6.17 kernel (evidence: test-results/unfiled4/). The host
// answers, both widths agreeing on every row:
//  - a vectored-read segment whose length is negative read as the
//    ABI's signed size answers -EINVAL before any byte moves,
//    wherever it sits in the array; the 32-bit side has no sum
//    check (1 GiB + 1 GiB + 1 reads normally);
//  - a positioned read on a pipe answers -ESPIPE ahead of the
//    iovcnt range, iovec array and flags answers (pread64: ahead
//    of its negative-offset -EINVAL); preadv's own negative
//    offset is -EINVAL ahead of ESPIPE; preadv2 offset -1 is the
//    file-position mode and reads from a pipe, with its flags
//    still validated first;
//  - linkat validates flags ahead of the null-path fault, then
//    follows the FD-131/FD-132 path rules, resolves dirfds, links
//    the symlink itself unless AT_SYMLINK_FOLLOW, and with
//    AT_EMPTY_PATH links the descriptor's file (a pipe descriptor
//    answers -EXDEV).

namespace {

struct TmpRoot {
    std::shared_ptr<FsFileNode> previous;
    char dirTemplate[64];
    bool ok = false;
    TmpRoot(const char* pattern) {
        snprintf(dirTemplate, sizeof(dirTemplate), "%s", pattern);
        char* dir = mkdtemp(dirTemplate);
        if (!dir) return;
        previous = Fs::rootNode;
        Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
        Fs::clearPathCache();
        ok = true;
    }
    ~TmpRoot() {
        if (previous) { Fs::rootNode = previous; Fs::clearPathCache(); }
    }
};

} // namespace

#ifndef BOXEDWINE_GUEST_X64

void testUnfiledGaps32() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    TmpRoot root("/tmp/bw64unfiled32XXXXXX");
    if (!root.ok) {
        testFail("unfiled-gaps 32-bit test could not create its temp root");
        return;
    }
    if (mkdir((std::string(root.dirTemplate) + "/sub").c_str(), 0755) != 0) {
        testFail("unfiled-gaps 32-bit test could not create its subdir");
        return;
    }

    KFileDescriptorPtr fileDesc;
    U32 oresult = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
    if ((S32)oresult < 0 || !fileDesc) {
        testFail("unfiled-gaps 32-bit test could not create /data.bin");
        return;
    }
    const char* text = "0123456789ABCDEFGHIJ";
    fileDesc->kobject->writeNative((U8*)text, 20);
    fileDesc->kobject->seek(0);
    const S32 fd = (S32)fileDesc->handle;

    KFileDescriptorPtr innerDesc;
    oresult = process->openFile(B("/"), B("/sub/inner.bin"), K_O_RDWR | K_O_CREAT, innerDesc);
    if ((S32)oresult < 0 || !innerDesc) {
        testFail("unfiled-gaps 32-bit test could not create /sub/inner.bin");
        return;
    }
    innerDesc->kobject->writeNative((U8*)"INNER", 5);

    FD txFd = 0, rxFd = 0;
    if (ksocketpairFds(context.thread, 1, 1, 0, 0, txFd, rxFd) != 0) {
        testFail("unfiled-gaps 32-bit test could not create its socketpair");
        return;
    }
    auto fillPipe = [&]() {
        KFileDescriptorPtr tx = process->getFileDescriptor(txFd);
        tx->kobject->writeNative((U8*)"wxyz", 4);
    };

    constexpr U32 iovAt = TEST_HEAP_ADDRESS + 0x400;
    constexpr U32 buf0 = TEST_HEAP_ADDRESS + 0x800;
    constexpr U32 buf1 = TEST_HEAP_ADDRESS + 0x900;
    constexpr U32 strAt = TEST_HEAP_ADDRESS + 0xC00;
    // Guest fixtures are staged inside invoke(), after
    // testNewInstruction: memory written before it does not
    // survive into the dispatched call (the shape
    // testPreadv32Dispatch established). setIov/setStr only
    // record; invoke replays every record, in order, and
    // zeroes the destination buffers so a segment the call
    // never touches reads back as zeros.
    U32 stagedBase[8];
    U32 stagedLen[8];
    U32 stagedCount = 0;
    auto setIov = [&](U32 idx, U32 base, U32 len) {
        stagedBase[idx] = base;
        stagedLen[idx] = len;
        if (idx + 1 > stagedCount) stagedCount = idx + 1;
    };
    std::vector<std::pair<U32, std::string>> stagedStrs;
    auto setStr = [&](U32 at, const char* s) {
        stagedStrs.push_back({at, std::string(s)});
    };
    auto segText = [&](U32 at, U32 len) {
        std::string s;
        for (U32 i = 0; i < len; i++) s += (char)context.memory->readb(at + i);
        return s;
    };
    // One generic int 0x80 invocation: ebx/ecx/edx/esi/edi/ebp.
    auto invoke = [&](U32 nr, U32 ebx, U32 ecx, U32 edx, U32 esi, U32 edi, U32 ebp) -> S32 {
        testNewInstruction(0);
        static const U8 zeros[256] = {};
        for (U32 i = 0; i < stagedCount; i++) {
            context.memory->writed(iovAt + i * 8 + 0, stagedBase[i]);
            context.memory->writed(iovAt + i * 8 + 4, stagedLen[i]);
            if (stagedLen[i] && stagedBase[i] >= TEST_HEAP_ADDRESS) {
                U32 n = stagedLen[i] <= 256 ? stagedLen[i] : 256;
                context.memory->memcpy(stagedBase[i], (void*)zeros, n);
            }
        }
        for (auto& kv : stagedStrs) {
            context.memory->memcpy(kv.first, (void*)kv.second.c_str(), (U32)kv.second.size() + 1);
        }
        testPushCode8(0xB8); testPushCode32(nr);   // mov eax, nr
        testPushCode8(0xCD); testPushCode8(0x80); // int 0x80
        context.cpu->reg[3].u32 = ebx;
        context.cpu->reg[1].u32 = ecx;
        context.cpu->reg[2].u32 = edx;
        context.cpu->reg[6].u32 = esi;
        context.cpu->reg[7].u32 = edi;
        context.cpu->reg[5].u32 = ebp;
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    auto readv = [&](S32 f, U32 cnt) -> S32 { return invoke(145, (U32)f, iovAt, cnt, 0, 0, 0); };
    auto preadv = [&](S32 f, U32 cnt, U32 lo, U32 hi) -> S32 { return invoke(333, (U32)f, iovAt, cnt, lo, hi, 0); };
    auto preadv2 = [&](S32 f, U32 cnt, U32 lo, U32 hi, U32 flags) -> S32 { return invoke(377, (U32)f, iovAt, cnt, lo, hi, flags); };
    auto pread64 = [&](S32 f, U32 cnt, U32 lo, U32 hi) -> S32 { return invoke(180, (U32)f, buf0, cnt, lo, hi, 0); };
    auto linkat = [&](S32 oldfd, U32 oldAt, S32 newfd, U32 newAt, U32 flags) -> S32 {
        return invoke(303, (U32)oldfd, oldAt, (U32)newfd, newAt, flags, 0);
    };
    constexpr U32 AT_FDCWD_U = (U32)-100;

    // ---- FD-193: readv segment lengths (socketpair source, so a
    // walk that wrongly proceeds returns the queued bytes instead
    // of the host's -EINVAL).
    fillPipe();
    setIov(0, buf0, 0x80000000u); // 2 GiB: negative as s32
    {
        S32 got = readv((S32)rxFd, 1);
        if (got != -K_EINVAL) {
            testFail("32-bit readv with a 2 GiB segment answered %d, not -EINVAL", got);
        }
    }
    setIov(0, buf0, 4);
    if (readv((S32)rxFd, 1) != 4 || segText(buf0, 4) != "wxyz") {
        testFail("32-bit readv after the refused segment lost the pipe bytes");
    }
    fillPipe();
    setIov(0, buf0, 2); setIov(1, buf1, 0xC0000000u); // bad segment second
    if (readv((S32)rxFd, 2) != -K_EINVAL) {
        testFail("32-bit readv with a negative second segment did not answer -EINVAL");
    }
    setIov(0, buf0, 4);
    if (readv((S32)rxFd, 1) != 4 || segText(buf0, 4) != "wxyz") {
        testFail("32-bit readv consumed pipe bytes ahead of the -EINVAL");
    }
    // No sum check on the 32-bit side: 1 GiB + 1 GiB + 1 against a
    // 20-byte file reads the file (short read ends the walk).
    setIov(0, buf0, 0x40000000u); setIov(1, buf1, 0x40000000u);
    setIov(2, buf0, 1);
    if (readv(fd, 3) != 20) {
        testFail("32-bit readv with segment sum 2^31+1 did not read the 20-byte file");
    }
    // preadv carries the same check (on the file here; the pipe
    // rows are ESPIPE territory, covered below).
    setIov(0, buf0, 0x80000000u);
    {
        S32 got = preadv(fd, 1, 0, 0);
        if (got != -K_EINVAL) {
            testFail("32-bit preadv with a 2 GiB segment answered %d, not -EINVAL", got);
        }
    }

    // ---- FD-194: the pipe matrix.
    fillPipe();
    setIov(0, buf0, 4);
    if (preadv((S32)rxFd, 1, 0, 0) != -K_ESPIPE) {
        testFail("32-bit preadv on a pipe did not answer -ESPIPE");
    }
    if (preadv((S32)rxFd, 2000, 0, 0) != -K_ESPIPE) {
        testFail("32-bit preadv on a pipe with iovcnt 2000 did not answer -ESPIPE ahead of the range check");
    }
    if (invoke(333, (U32)rxFd, 0, 1, 0, 0, 0) != -K_ESPIPE) {
        testFail("32-bit preadv on a pipe with a null array did not answer -ESPIPE ahead of the array check");
    }
    if (preadv((S32)txFd, 1, 0, 0) != -K_ESPIPE) {
        testFail("32-bit preadv on the pipe write end did not answer -ESPIPE");
    }
    if (preadv((S32)rxFd, 1, 0xFFFFFFFFu, 0xFFFFFFFFu) != -K_EINVAL) {
        testFail("32-bit preadv on a pipe with offset -1 did not answer -EINVAL ahead of ESPIPE");
    }
    if (pread64((S32)rxFd, 4, 0, 0) != -K_ESPIPE) {
        testFail("32-bit pread64 on a pipe did not answer -ESPIPE");
    }
    if (pread64((S32)rxFd, 4, 0xFFFFFFFEu, 0xFFFFFFFFu) != -K_ESPIPE) {
        testFail("32-bit pread64 on a pipe with offset -2 did not answer -ESPIPE ahead of the offset check");
    }
    if (preadv2((S32)rxFd, 1, 0, 0, 0) != -K_ESPIPE) {
        testFail("32-bit preadv2 on a pipe did not answer -ESPIPE");
    }
    if (preadv2((S32)rxFd, 1, 0xFFFFFFFEu, 0xFFFFFFFFu, 0) != -K_EINVAL) {
        testFail("32-bit preadv2 on a pipe with offset -2 did not answer -EINVAL");
    }
    if (preadv2((S32)rxFd, 1, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x8000u) != -K_EOPNOTSUPP) {
        testFail("32-bit preadv2 file-position mode on a pipe did not validate flags (-EOPNOTSUPP)");
    }
    // File-position mode reads from the pipe; the bytes queued
    // above must still be intact (no refused call consumed any).
    setIov(0, buf0, 4);
    if (preadv2((S32)rxFd, 1, 0xFFFFFFFFu, 0xFFFFFFFFu, 0) != 4 || segText(buf0, 4) != "wxyz") {
        testFail("32-bit preadv2 file-position mode did not read the intact pipe bytes");
    }

    // Write twins (FD-194): the positioned-write family takes
    // the same answers. The pipe is empty here (the reads above
    // drained it); the position-mode write refills it and the
    // plain readv proves the bytes arrived.
    auto pwritev = [&](S32 f, U32 cnt, U32 lo, U32 hi) -> S32 { return invoke(334, (U32)f, iovAt, cnt, lo, hi, 0); };
    auto pwritev2 = [&](S32 f, U32 cnt, U32 lo, U32 hi, U32 flags) -> S32 { return invoke(378, (U32)f, iovAt, cnt, lo, hi, flags); };
    auto pwrite64 = [&](S32 f, U32 cnt, U32 lo, U32 hi) -> S32 { return invoke(181, (U32)f, buf0, cnt, lo, hi, 0); };
    setIov(0, buf0, 4);
    setStr(buf0, "wxyz");
    if (pwritev((S32)txFd, 1, 0, 0) != -K_ESPIPE) {
        testFail("32-bit pwritev on a pipe did not answer -ESPIPE");
    }
    if (pwritev((S32)txFd, 2000, 0, 0) != -K_ESPIPE) {
        testFail("32-bit pwritev on a pipe with iovcnt 2000 did not answer -ESPIPE ahead of the range check");
    }
    if (pwritev((S32)rxFd, 1, 0, 0) != -K_ESPIPE) {
        testFail("32-bit pwritev on the pipe read end did not answer -ESPIPE");
    }
    if (pwrite64((S32)txFd, 4, 0, 0) != -K_ESPIPE) {
        testFail("32-bit pwrite64 on a pipe did not answer -ESPIPE");
    }
    S32 pw32 = pwritev2((S32)txFd, 1, 0xFFFFFFFFu, 0xFFFFFFFFu, 0);
    if (pw32 != 4) {
        testFail("32-bit pwritev2 file-position mode did not write to the pipe");
    }
    if (pw32 == 4) { // drain only when the write landed, so a failing build fails instead of blocking on an empty pipe
        setIov(0, buf1, 4);
        if (readv((S32)rxFd, 1) != 4 || segText(buf1, 4) != "wxyz") {
            testFail("32-bit pwritev2 file-position mode bytes did not arrive");
        }
    }

    // ---- FD-195: linkat.
    setStr(strAt, "/data.bin");
    setStr(strAt + 0x40, "/copy.bin");
    {
        S32 got = linkat((S32)AT_FDCWD_U, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0);
        if (got != 0) {
            testFail("32-bit linkat basic link answered %d, not 0", got);
        }
    }
    {
        KFileDescriptorPtr copyDesc;
        U32 r = process->openFile(B("/"), B("/copy.bin"), K_O_RDONLY, copyDesc);
        if ((S32)r < 0 || !copyDesc) {
            testFail("32-bit linkat did not create /copy.bin");
        } else {
            U8 tmp[20];
            copyDesc->kobject->seek(0);
            U32 got = copyDesc->kobject->readNative(tmp, 20);
            if (got != 20 || memcmp(tmp, text, 20) != 0) {
                testFail("32-bit linkat target does not hold the source bytes");
            }
        }
    }
    if (linkat((S32)AT_FDCWD_U, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0) != -K_EEXIST) {
        testFail("32-bit linkat onto an existing name did not answer -EEXIST");
    }
    if (linkat((S32)AT_FDCWD_U, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0x8000u) != -K_EINVAL) {
        testFail("32-bit linkat with flags 0x8000 did not answer -EINVAL");
    }
    if (linkat((S32)AT_FDCWD_U, 0, (S32)AT_FDCWD_U, strAt + 0x40, 0x8000u) != -K_EINVAL) {
        testFail("32-bit linkat flags check did not precede the null-path fault");
    }
    if (linkat((S32)AT_FDCWD_U, 0, (S32)AT_FDCWD_U, strAt + 0x40, 0) != -K_EFAULT) {
        testFail("32-bit linkat with a null old path did not answer -EFAULT");
    }
    if (linkat((S32)AT_FDCWD_U, strAt, (S32)AT_FDCWD_U, 0, 0) != -K_EFAULT) {
        testFail("32-bit linkat with a null new path did not answer -EFAULT");
    }
    setStr(strAt + 0x80, "");
    if (linkat((S32)AT_FDCWD_U, strAt + 0x80, (S32)AT_FDCWD_U, strAt + 0x40, 0) != -K_ENOENT) {
        testFail("32-bit linkat with an empty old path did not answer -ENOENT");
    }
    if (linkat((S32)AT_FDCWD_U, strAt, (S32)AT_FDCWD_U, strAt + 0x80, 0) != -K_ENOENT) {
        testFail("32-bit linkat with an empty new path did not answer -ENOENT");
    }
    setStr(strAt, "/missing.bin");
    if (linkat((S32)AT_FDCWD_U, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0) != -K_ENOENT) {
        testFail("32-bit linkat of a missing source did not answer -ENOENT");
    }
    setStr(strAt, "/sub");
    setStr(strAt + 0x40, "/dirlink.bin");
    if (linkat((S32)AT_FDCWD_U, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0) != -K_EPERM) {
        testFail("32-bit linkat of a directory did not answer -EPERM");
    }
    setStr(strAt, "inner.bin");
    {
        S32 got = linkat(9999, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0);
        if (got != -K_EBADF) {
            testFail("32-bit linkat with a bad olddirfd answered %d, not -EBADF", got);
        }
    }
    if (linkat(fd, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0) != -K_ENOTDIR) {
        testFail("32-bit linkat with a file as olddirfd did not answer -ENOTDIR");
    }
    // dirfd-relative resolution through the real dispatcher: open
    // /sub by the dispatched openat (nr 295), then link out of it.
    setStr(strAt + 0xC0, "/sub");
    S32 dirFd = invoke(295, AT_FDCWD_U, strAt + 0xC0, 0, 0, 0, 0);
    if (dirFd < 0) {
        testFail("32-bit linkat test could not open /sub for its dirfd (openat answered %d)", dirFd);
    } else {
        setStr(strAt + 0x40, "/innercopy.bin");
        if (linkat(dirFd, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0) != 0) {
            testFail("32-bit linkat did not resolve its source against the olddirfd");
        }
    }
    // AT_EMPTY_PATH descriptor form.
    setStr(strAt + 0x80, "");
    setStr(strAt + 0x40, "/fdlink.bin");
    if (linkat(fd, strAt + 0x80, (S32)AT_FDCWD_U, strAt + 0x40, 0x1000u) != 0) {
        testFail("32-bit linkat AT_EMPTY_PATH on a file descriptor did not link it");
    }
    if (linkat(fd, strAt + 0x80, (S32)AT_FDCWD_U, strAt + 0x40, 0) != -K_ENOENT) {
        testFail("32-bit linkat empty old path without AT_EMPTY_PATH did not answer -ENOENT");
    }
    setStr(strAt + 0x40, "/pipelink.bin");
    if (linkat((S32)rxFd, strAt + 0x80, (S32)AT_FDCWD_U, strAt + 0x40, 0x1000u) != -K_EXDEV) {
        testFail("32-bit linkat AT_EMPTY_PATH on a pipe descriptor did not answer -EXDEV");
    }
    // Symlink handling: without the flag the link names the
    // symlink itself; with it, the target.
    if ((S32)process->symlink(B("/data.bin"), B("/alink")) < 0) {
        testFail("32-bit linkat test could not create its symlink");
        return;
    }
    setStr(strAt, "/alink");
    setStr(strAt + 0x40, "/alinkcopy");
    if (linkat((S32)AT_FDCWD_U, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0) != 0) {
        testFail("32-bit linkat of a symlink did not return 0");
    }
    {
        U32 rl = process->readlink(B("/alinkcopy"), buf0, 64);
        if ((S32)rl < 0 || segText(buf0, rl) != "/data.bin") {
            testFail("32-bit linkat without AT_SYMLINK_FOLLOW did not link the symlink itself");
        }
    }
    setStr(strAt + 0x40, "/alinkcopy2");
    if (linkat((S32)AT_FDCWD_U, strAt, (S32)AT_FDCWD_U, strAt + 0x40, 0x400u) != 0) {
        testFail("32-bit linkat with AT_SYMLINK_FOLLOW did not return 0");
    }
    {
        U32 rl = process->readlink(B("/alinkcopy2"), buf0, 64);
        if ((S32)rl >= 0) {
            testFail("32-bit linkat with AT_SYMLINK_FOLLOW linked the symlink, not its target");
        }
    }
}

#endif // !BOXEDWINE_GUEST_X64

#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kmemory64.h"

S32 ksyscall64(CPU64* cpu);

void testUnfiledGaps64() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x7B000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("unfiled-gaps x64 test could not map its fixture page");
        return;
    }
    TmpRoot root("/tmp/bw64unfiled64XXXXXX");
    if (!root.ok) {
        testFail("unfiled-gaps x64 test could not create its temp root");
        return;
    }
    // A throwaway descriptor first: the 64-bit read path treats
    // descriptor 0 as stdin and always answers end-of-file
    // (sys_read64), so the data file must not land on handle 0.
    KFileDescriptorPtr dummyDesc;
    process->openFile(B("/"), B("/dummy.bin"), K_O_RDWR | K_O_CREAT, dummyDesc);
    KFileDescriptorPtr fileDesc;
    U32 oresult = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
    if ((S32)oresult < 0 || !fileDesc) {
        testFail("unfiled-gaps x64 test could not create /data.bin");
        return;
    }
    const char* text = "0123456789ABCDEFGHIJ";
    fileDesc->kobject->writeNative((U8*)text, 20);
    fileDesc->kobject->seek(0);
    const S32 fd = (S32)fileDesc->handle;

    FD txFd = 0, rxFd = 0;
    if (ksocketpairFds(context.thread, 1, 1, 0, 0, txFd, rxFd) != 0) {
        testFail("unfiled-gaps x64 test could not create its socketpair");
        return;
    }
    auto fillPipe = [&]() {
        KFileDescriptorPtr tx = process->getFileDescriptor(txFd);
        tx->kobject->writeNative((U8*)"wxyz", 4);
    };

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    constexpr U64 iovAt = page + 0x100;
    constexpr U64 seg0 = page + 0x400;
    constexpr U64 seg1 = page + 0x500;
    auto setIov = [&](U32 idx, U64 base, U64 len) {
        memory->writeq(iovAt + idx * 16, base);
        memory->writeq(iovAt + idx * 16 + 8, len);
    };
    auto segText = [&](U64 at, U32 len) {
        std::string s;
        for (U32 i = 0; i < len; i++) s += (char)memory->readb(at + i);
        return s;
    };
    auto readv = [&](S32 f, U64 cnt) -> S64 { return invoke6(19, (U64)(U32)f, iovAt, cnt, 0, 0, 0); };
    auto preadv = [&](S32 f, U64 cnt, U64 lo, U64 hi) -> S64 { return invoke6(295, (U64)(U32)f, iovAt, cnt, lo, hi, 0); };
    auto preadv2 = [&](S32 f, U64 cnt, U64 lo, U64 hi, U64 flags) -> S64 { return invoke6(327, (U64)(U32)f, iovAt, cnt, lo, hi, flags); };
    auto pread64 = [&](S32 f, U64 cnt, U64 off) -> S64 { return invoke6(17, (U64)(U32)f, seg0, cnt, off, 0, 0); };

    // ---- FD-193: readv segment lengths.
    fillPipe();
    setIov(0, seg0, 0x8000000000000000ULL); // negative as s64
    if (readv((S32)rxFd, 1) != -K_EINVAL) {
        testFail("x64 readv with a top-bit segment did not answer -EINVAL");
    }
    setIov(0, seg0, 4);
    if (readv((S32)rxFd, 1) != 4 || segText(seg0, 4) != "wxyz") {
        testFail("x64 readv after the refused segment lost the pipe bytes");
    }
    fillPipe();
    setIov(0, seg0, 2); setIov(1, seg1, 0x8000000000000000ULL);
    if (readv((S32)rxFd, 2) != -K_EINVAL) {
        testFail("x64 readv with a top-bit second segment did not answer -EINVAL");
    }
    setIov(0, seg0, 4);
    if (readv((S32)rxFd, 1) != 4 || segText(seg0, 4) != "wxyz") {
        testFail("x64 readv consumed pipe bytes ahead of the -EINVAL");
    }
    // Two segments that each fit read the file (the host's range
    // -EFAULT for ranges beyond the address space is a stated
    // boundary of FD-193, not reproduced here).
    setIov(0, seg0, 0x4000000000000000ULL); setIov(1, seg1, 0x4000000000000000ULL);
    if (readv(fd, 2) != 20) {
        testFail("x64 readv with two 2^62 segments did not read the 20-byte file");
    }
    setIov(0, seg0, 0x8000000000000000ULL);
    if (preadv(fd, 1, 0, 0) != -K_EINVAL) {
        testFail("x64 preadv with a top-bit segment did not answer -EINVAL");
    }

    // ---- FD-194: the pipe matrix.
    fillPipe();
    setIov(0, seg0, 4);
    if (preadv((S32)rxFd, 1, 0, 0) != -K_ESPIPE) {
        testFail("x64 preadv on a pipe did not answer -ESPIPE");
    }
    if (preadv((S32)rxFd, 2000, 0, 0) != -K_ESPIPE) {
        testFail("x64 preadv on a pipe with iovcnt 2000 did not answer -ESPIPE ahead of the range check");
    }
    if (invoke6(295, (U64)(U32)rxFd, 0, 1, 0, 0, 0) != -K_ESPIPE) {
        testFail("x64 preadv on a pipe with a null array did not answer -ESPIPE ahead of the array check");
    }
    if (preadv((S32)txFd, 1, 0, 0) != -K_ESPIPE) {
        testFail("x64 preadv on the pipe write end did not answer -ESPIPE");
    }
    if (preadv((S32)rxFd, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL) != -K_EINVAL) {
        testFail("x64 preadv on a pipe with offset -1 did not answer -EINVAL ahead of ESPIPE");
    }
    if (pread64((S32)rxFd, 4, 0) != -K_ESPIPE) {
        testFail("x64 pread64 on a pipe did not answer -ESPIPE (it must not stream the bytes)");
    }
    if (pread64((S32)rxFd, 4, 0xFFFFFFFFFFFFFFFEULL) != -K_ESPIPE) {
        testFail("x64 pread64 on a pipe with offset -2 did not answer -ESPIPE ahead of the offset check");
    }
    if (preadv2((S32)rxFd, 1, 0, 0, 0) != -K_ESPIPE) {
        testFail("x64 preadv2 on a pipe did not answer -ESPIPE");
    }
    if (preadv2((S32)rxFd, 1, 0xFFFFFFFEULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("x64 preadv2 on a pipe with offset -2 did not answer -EINVAL");
    }
    if (preadv2((S32)rxFd, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0x8000ULL) != -K_EOPNOTSUPP) {
        testFail("x64 preadv2 file-position mode on a pipe did not validate flags (-EOPNOTSUPP)");
    }
    setIov(0, seg0, 4);
    if (preadv2((S32)rxFd, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0) != 4 || segText(seg0, 4) != "wxyz") {
        testFail("x64 preadv2 file-position mode did not read the intact pipe bytes");
    }

    // Write twins (FD-194): the positioned-write family takes
    // the same answers; the native path additionally answers
    // -EINVAL for pwrite64 with a negative offset even on a
    // pipe (the compat path answers -ESPIPE there -- the two
    // cores follow their own ABI, as measured).
    auto pwritev = [&](S32 f, U64 cnt, U64 lo, U64 hi) -> S64 { return invoke6(296, (U64)(U32)f, iovAt, cnt, lo, hi, 0); };
    auto pwritev2 = [&](S32 f, U64 cnt, U64 lo, U64 hi, U64 flags) -> S64 { return invoke6(328, (U64)(U32)f, iovAt, cnt, lo, hi, flags); };
    auto pwrite64 = [&](S32 f, U64 cnt, U64 off) -> S64 { return invoke6(18, (U64)(U32)f, seg0, cnt, off, 0, 0); };
    memory->memcpyToGuest(seg0, "wxyz", 4);
    setIov(0, seg0, 4);
    if (pwritev((S32)txFd, 1, 0, 0) != -K_ESPIPE) {
        testFail("x64 pwritev on a pipe did not answer -ESPIPE");
    }
    if (pwritev((S32)txFd, 2000, 0, 0) != -K_ESPIPE) {
        testFail("x64 pwritev on a pipe with iovcnt 2000 did not answer -ESPIPE ahead of the range check");
    }
    if (pwritev((S32)rxFd, 1, 0, 0) != -K_ESPIPE) {
        testFail("x64 pwritev on the pipe read end did not answer -ESPIPE");
    }
    if (pwrite64((S32)txFd, 4, 0) != -K_ESPIPE) {
        testFail("x64 pwrite64 on a pipe did not answer -ESPIPE (it must not stream the bytes)");
    }
    if (pwrite64((S32)txFd, 4, 0xFFFFFFFFFFFFFFFEULL) != -K_EINVAL) {
        testFail("x64 pwrite64 on a pipe with offset -2 did not answer -EINVAL (the native order)");
    }
    S64 pw64 = pwritev2((S32)txFd, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0);
    if (pw64 != 4) {
        testFail("x64 pwritev2 file-position mode did not write to the pipe");
    }
    if (pw64 == 4) { // drain only when the write landed, so a failing build fails instead of blocking on an empty pipe
        setIov(0, seg1, 4);
        if (readv((S32)rxFd, 1) != 4 || segText(seg1, 4) != "wxyz") {
            testFail("x64 pwritev2 file-position mode bytes did not arrive");
        }
    }
}

#endif // BOXEDWINE_GUEST_X64

#endif // __TEST
