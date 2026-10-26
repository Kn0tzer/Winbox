/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License; either version 2
 *  of the License, or (at your option) any later version.
 */

#include "boxedwine.h"

#ifdef __TEST
#ifdef BOXEDWINE_GUEST_X64

#include <string>

#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "../cpu/testCPU.h"
#include "../../io/fsfilenode.h"
#include "bufferaccess.h"
#include "meminfo.h"

namespace {
bool parseMeminfoKb(const std::string& text, const char* key, U64& value) {
    const size_t at = text.find(key);
    if (at == std::string::npos) return false;
    value = std::strtoull(text.c_str() + at + std::strlen(key), nullptr, 10);
    return true;
}
}

// K-M5: sysinfo(2) memory figures must be derived from the guest memory
// allocation, not hardcoded constants: freeram falls as pages are committed
// and recovers when they are released, and /proc/meminfo must state exactly
// what sysinfo states.
void testSysinfo64Consistency() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 scratch = 0x77400000ULL;
    if (!memory->getPageFlags(scratch >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(scratch, K64_PAGE_SIZE * 2, 3) != scratch) {
        testFail("x64 sysinfo test could not map a scratch page");
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto sysinfo = [&](U64& total, U64& free, U64& procs, U64& memUnit) -> S64 {
        cpu.reg[X64_RAX].setU64(99); // __NR_sysinfo
        cpu.reg[X64_RDI].setU64(scratch);
        ksyscall64(&cpu);
        total = memory->readq(scratch + 32);
        free = memory->readq(scratch + 40);
        procs = memory->readw(scratch + 80);
        memUnit = memory->readd(scratch + 104);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    U64 total0 = 0, free0 = 0, procs = 0, memUnit = 0;
    if (sysinfo(total0, free0, procs, memUnit) != 0) {
        testFail("x64 sysinfo returned an error");
        return;
    }
    if (total0 != 1024ULL * 1024 * 1024 || memUnit != 1 || procs < 1 || free0 == 0 || free0 > total0) {
        testFail("x64 sysinfo baseline is wrong: total=%llu free=%llu procs=%llu mem_unit=%llu",
                 (unsigned long long)total0, (unsigned long long)free0,
                 (unsigned long long)procs, (unsigned long long)memUnit);
        return;
    }
    // /proc/meminfo must agree with sysinfo exactly while nothing commits.
    static std::shared_ptr<FsNode> dummyNode =
        std::make_shared<FsFileNode>(1, 0, B("/sysinfo-test"), B(""), B("/tmp"), true, true, nullptr);
    auto readMeminfo = [&]() {
        std::string result;
        FsOpenNode* node = openMemInfo(dummyNode, 0, 0);
        if (!node) return result;
        U8 buffer[256];
        const U32 read = node->readNative(buffer, sizeof(buffer));
        result.assign((const char*)buffer, read);
        delete node;
        return result;
    };
    U64 memTotalKb = 0, memFreeKb = 0;
    const std::string meminfo = readMeminfo();
    if (!parseMeminfoKb(meminfo, "MemTotal:", memTotalKb) || !parseMeminfoKb(meminfo, "MemFree:", memFreeKb) ||
        memTotalKb * 1024 != total0 || memFreeKb * 1024 != free0) {
        testFail("/proc/meminfo (%llu kB total, %llu kB free) disagrees with sysinfo (%llu, %llu)",
                 (unsigned long long)memTotalKb, (unsigned long long)memFreeKb,
                 (unsigned long long)total0, (unsigned long long)free0);
        return;
    }
    // Committing 64 MiB must reduce freeram by that amount; the total must
    // not move. A hardcoded freeram cannot pass this.
    constexpr U64 commitBase = 0x77500000ULL;
    constexpr U64 commitBytes = 64ULL * 1024 * 1024;
    if (memory->mmapAnonymousFixed(commitBase, commitBytes, 3) != commitBase) {
        testFail("x64 sysinfo test could not map the commit range");
        return;
    }
    memory->memsetGuest(commitBase, 0xA5, commitBytes);
    U64 total1 = 0, free1 = 0, procs1 = 0, unit1 = 0;
    sysinfo(total1, free1, procs1, unit1);
    const U64 dropped = free0 - free1;
    if (total1 != total0 || dropped < commitBytes - 2 * 1024 * 1024 || dropped > commitBytes + 8 * 1024 * 1024) {
        testFail("x64 sysinfo freeram did not track a 64 MiB commit: before=%llu after=%llu total=%llu",
                 (unsigned long long)free0, (unsigned long long)free1, (unsigned long long)total1);
        memory->munmap(commitBase, commitBytes);
        return;
    }
    memory->munmap(commitBase, commitBytes);
    U64 total2 = 0, free2 = 0, procs2 = 0, unit2 = 0;
    sysinfo(total2, free2, procs2, unit2);
    if (total2 != total0 || free2 > free0 || free0 - free2 > 8 * 1024 * 1024) {
        testFail("x64 sysinfo freeram did not recover after munmap: before=%llu after=%llu",
                 (unsigned long long)free0, (unsigned long long)free2);
    }
}

#endif
#endif
