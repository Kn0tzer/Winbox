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
#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kerror.h"
#include "kfile.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "../../io/fsfilenode.h"
#include "../../io/fsopennode.h"
#include "../cpu/testCPU.h"

#include <stdlib.h>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// FD-92: an open file description whose positioned reads serve a
// fixed pattern up to a byte budget and then fail with -EIO -- the
// errored positioned read the register entry describes, produced at
// the FsOpenNode::readNative interface whose result
// KFile::preadNative forwards verbatim (sys_mmap64_file's content
// loop, like sys_pread64's core, is written against (S32)result < 0
// as the error answer).
class ErroringOpenNode : public FsOpenNode {
public:
    ErroringOpenNode(std::shared_ptr<FsNode> node, U64 serveBudget)
        : FsOpenNode(node, K_O_RDONLY), budget(serveBudget) {
    }

    std::vector<U8> content;

    S64 length() override { return (S64)this->content.size(); }
    bool setLength(S64) override { return false; }
    S64 getFilePointer() override { return this->pos; }
    S64 seek(S64 p) override { this->pos = p; return this->pos; }
    U32 map(KThread*, U32, U32, S32, S32, U64) override { return (U32)-K_ENODEV; }
    bool canMap() override { return false; }
    U32 ioctl(KThread*, U32) override { return (U32)-K_ENOTTY; }
    void waitForEvents(BOXEDWINE_CONDITION&, U32) override {}
    bool isWriteReady() override { return false; }
    bool isReadReady() override { return true; }
    U32 readNative(U8* buffer, U32 len) override {
        if (this->served >= this->budget) return (U32)-K_EIO;
        U64 n = len;
        U64 budgetLeft = this->budget - this->served;
        if (n > budgetLeft) n = budgetLeft;
        U64 fileLeft = 0;
        if (this->pos >= 0 && (U64)this->pos < this->content.size()) {
            fileLeft = this->content.size() - (U64)this->pos;
        }
        if (n > fileLeft) n = fileLeft;
        if (n == 0) return 0;
        memcpy(buffer, this->content.data() + (size_t)this->pos, (size_t)n);
        this->pos += (S64)n;
        this->served += n;
        return (U32)n;
    }
    U32 writeNative(U8*, U32) override { return (U32)-K_EBADF; }
    void close() override {}
    void reopen() override {}
    bool isOpen() override { return true; }

private:
    U64 budget;
    U64 served = 0;
    S64 pos = 0;
};

U8 patternByte(U32 i) { return (U8)(0x3C + (i % 61)); }

} // namespace

// FD-92: sys_mmap64_file establishes the mapping (an explicit-address
// map, or the addr==0 reservation) before it copies the file content
// in, and its content-read error return used to leave both behind:
// the syscall reported failure while the range stayed mapped and
// unavailable for reuse. A failed host mmap leaves nothing mapped
// and nothing reserved. Every case drives the real 64-bit syscall
// dispatch; the addr==0 cases locate the reservation through a
// control allocation, because the mmap gap search starts at the
// cursor the control allocation leaves behind.
void testMmap64FileReadFailure() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    char dirTemplate[] = "/tmp/bw64mmapfailXXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for mmap64 file read failure test");
        return;
    }
    auto rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    auto fileNode = std::make_shared<FsFileNode>(9201, 0, B("/failread.bin"), B(""), BString::copy(dir), false, false, rootNode);

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
    auto sysMmap = [&](U64 addr, U64 len, U64 prot, U64 flags, S32 fd) -> S64 {
        return invoke6(9, addr, len, prot, flags, (U64)(U32)fd, 0);
    };
    auto sysMunmap = [&](U64 addr, U64 len) -> S64 {
        return invoke6(11, addr, len, 0, 0, 0, 0);
    };
    auto sysClose = [&](S32 fd) -> S64 {
        return invoke6(3, (U64)(U32)fd, 0, 0, 0, 0, 0);
    };
    auto mapped = [&](U64 addr) -> bool {
        return (memory->getPageFlags(addr >> K64_PAGE_SHIFT) & K64_PAGE_MAPPED) != 0;
    };
    auto makeFd = [&](U64 serveBudget, U32 contentLen) -> S32 {
        auto* openNode = new ErroringOpenNode(fileNode, serveBudget);
        openNode->content.resize(contentLen);
        for (U32 i = 0; i < contentLen; i++) openNode->content[i] = patternByte(i);
        auto kfile = std::make_shared<KFile>(openNode);
        KFileDescriptorPtr desc = process->allocFileDescriptor(kfile, K_O_RDONLY, 0, -1, 0);
        if (!desc) return -1;
        return (S32)desc->handle; // the fd table holds the description
    };

    constexpr U64 PROT_R = 1;
    constexpr U64 PROT_RW = 3;
    constexpr U64 MAP_SHARED_ = 0x01;
    constexpr U64 MAP_PRIVATE_ = 0x02;
    constexpr U64 MAP_ANON_ = 0x20;
    constexpr U64 TWO_PAGES = 2 * K64_PAGE_SIZE;

    // Case A: explicit address, MAP_PRIVATE, the content read fails
    // on the first byte. The error must come back AND the range must
    // be left unmapped and reusable.
    {
        constexpr U64 addr = 0x100000000ULL;
        if (mapped(addr) || mapped(addr + K64_PAGE_SIZE)) {
            testFail("case A: fixture address unexpectedly already mapped");
            return;
        }
        S32 fd = makeFd(0, (U32)TWO_PAGES);
        if (fd < 0) { testFail("case A: could not allocate the failing descriptor"); return; }
        S64 r = sysMmap(addr, TWO_PAGES, PROT_R, MAP_PRIVATE_, fd);
        if (r != -K_EIO) {
            testFail("case A: mmap must return the read error (-EIO), got %lld", (long long)r);
            if (r > 0) sysMunmap(addr, TWO_PAGES);
        } else {
            if (mapped(addr) || mapped(addr + K64_PAGE_SIZE)) {
                testFail("case A: failed mmap left pages mapped at 0x%llx", (unsigned long long)addr);
                sysMunmap(addr, TWO_PAGES); // keep later cases honest on unfixed code
            } else if (sysMmap(addr, TWO_PAGES, PROT_RW, MAP_PRIVATE_ | MAP_ANON_, -1) != (S64)addr) {
                testFail("case A: the range a failed mmap touched is not reusable");
            } else {
                memory->writeb(addr, 0x5A);
                if (memory->readb(addr) != 0x5A) {
                    testFail("case A: reused range does not hold writes");
                }
                sysMunmap(addr, TWO_PAGES);
            }
        }
        sysClose(fd);
    }

    // Case B: explicit address, MAP_PRIVATE, the read serves one
    // page and then fails -- a failure partway through the copy is
    // still a failed mmap and must leave nothing behind.
    {
        constexpr U64 addr = 0x100100000ULL;
        if (mapped(addr) || mapped(addr + K64_PAGE_SIZE)) {
            testFail("case B: fixture address unexpectedly already mapped");
            return;
        }
        S32 fd = makeFd(K64_PAGE_SIZE, (U32)TWO_PAGES);
        if (fd < 0) { testFail("case B: could not allocate the failing descriptor"); return; }
        S64 r = sysMmap(addr, TWO_PAGES, PROT_R, MAP_PRIVATE_, fd);
        if (r != -K_EIO) {
            testFail("case B: mmap must return the read error (-EIO), got %lld", (long long)r);
            if (r > 0) sysMunmap(addr, TWO_PAGES);
        } else {
            if (mapped(addr) || mapped(addr + K64_PAGE_SIZE)) {
                testFail("case B: partially-read failed mmap left pages mapped at 0x%llx", (unsigned long long)addr);
                sysMunmap(addr, TWO_PAGES);
            } else if (sysMmap(addr, TWO_PAGES, PROT_RW, MAP_PRIVATE_ | MAP_ANON_, -1) != (S64)addr) {
                testFail("case B: the range a failed mmap touched is not reusable");
            } else {
                sysMunmap(addr, TWO_PAGES);
            }
        }
        sysClose(fd);
    }

    // Case C: control -- the same description shape with a read that
    // never fails maps successfully and serves the content, so the
    // failure cases above discriminate on the error path only.
    {
        constexpr U64 addr = 0x100200000ULL;
        if (mapped(addr) || mapped(addr + K64_PAGE_SIZE)) {
            testFail("case C: fixture address unexpectedly already mapped");
            return;
        }
        S32 fd = makeFd(~0ULL, (U32)TWO_PAGES);
        if (fd < 0) { testFail("case C: could not allocate the control descriptor"); return; }
        S64 r = sysMmap(addr, TWO_PAGES, PROT_R, MAP_PRIVATE_, fd);
        if (r != (S64)addr) {
            testFail("case C: control mmap failed: %lld", (long long)r);
        } else {
            if (memory->readb(addr) != patternByte(0) ||
                memory->readb(addr + TWO_PAGES - 1) != patternByte((U32)TWO_PAGES - 1)) {
                testFail("case C: control mapping does not serve the file content");
            }
            if (sysMunmap(addr, TWO_PAGES) != 0 || mapped(addr)) {
                testFail("case C: control mapping does not unmap cleanly");
            }
        }
        sysClose(fd);
    }

    // Case D: addr==0, MAP_PRIVATE, the read fails on the first
    // byte. The reservation the syscall took before the read must
    // be released. A control allocation pins the mmap cursor, so
    // the reservation the failing call takes is the two pages right
    // after the control range.
    {
        S64 control = sysMmap(0, 3 * K64_PAGE_SIZE, PROT_RW, MAP_PRIVATE_ | MAP_ANON_, -1);
        if (control < 0) { testFail("case D: control reservation failed"); return; }
        U64 target = (U64)control + 3 * K64_PAGE_SIZE;
        if (mapped(target) || mapped(target + K64_PAGE_SIZE)) {
            testFail("case D: cursor target unexpectedly already mapped");
            sysMunmap((U64)control, 3 * K64_PAGE_SIZE);
            return;
        }
        sysMunmap((U64)control, 3 * K64_PAGE_SIZE);
        S32 fd = makeFd(0, (U32)TWO_PAGES);
        if (fd < 0) { testFail("case D: could not allocate the failing descriptor"); return; }
        S64 r = sysMmap(0, TWO_PAGES, PROT_R, MAP_PRIVATE_, fd);
        if (r != -K_EIO) {
            testFail("case D: mmap must return the read error (-EIO), got %lld", (long long)r);
            if (r > 0) sysMunmap((U64)r, TWO_PAGES);
        } else if (mapped(target) || mapped(target + K64_PAGE_SIZE)) {
            testFail("case D: failed mmap left the reserved range mapped at 0x%llx", (unsigned long long)target);
            sysMunmap(target, TWO_PAGES);
        }
        sysClose(fd);
    }

    // Case E: addr==0, MAP_SHARED, the seed read fails before the
    // shared pages are installed -- but after the reservation was
    // taken. The reservation must be released the same way.
    {
        S64 control = sysMmap(0, 3 * K64_PAGE_SIZE, PROT_RW, MAP_PRIVATE_ | MAP_ANON_, -1);
        if (control < 0) { testFail("case E: control reservation failed"); return; }
        U64 target = (U64)control + 3 * K64_PAGE_SIZE;
        if (mapped(target) || mapped(target + K64_PAGE_SIZE)) {
            testFail("case E: cursor target unexpectedly already mapped");
            sysMunmap((U64)control, 3 * K64_PAGE_SIZE);
            return;
        }
        sysMunmap((U64)control, 3 * K64_PAGE_SIZE);
        S32 fd = makeFd(0, (U32)TWO_PAGES);
        if (fd < 0) { testFail("case E: could not allocate the failing descriptor"); return; }
        S64 r = sysMmap(0, TWO_PAGES, PROT_R, MAP_SHARED_, fd);
        if (r != -K_EIO) {
            testFail("case E: mmap must return the read error (-EIO), got %lld", (long long)r);
            if (r > 0) sysMunmap((U64)r, TWO_PAGES);
        } else if (mapped(target) || mapped(target + K64_PAGE_SIZE)) {
            testFail("case E: failed shared mmap left the reserved range mapped at 0x%llx", (unsigned long long)target);
            sysMunmap(target, TWO_PAGES);
        }
        sysClose(fd);
    }
}

#endif
#endif
