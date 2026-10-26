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

#include <fstream>
#include <string>
#include <iterator>

#include "../cpu/testCPU.h"
#include "../../io/fsfilenode.h"
#include "bufferaccess.h"
#include "cpuinfo.h"
#include "meminfo.h"
#include "procstat.h"
#include "syscpuonline.h"

// The virtual machine exposes exactly one schedulable guest CPU
// (CPU64::affinityMask, sched_getaffinity). Every other guest-visible
// topology surface must agree with that single-CPU topology no matter how
// many CPUs the host has: glibc derives _SC_NPROCESSORS_ONLN from
// /sys/devices/system/cpu/online and _SC_NPROCESSORS_CONF from cpuinfo /
// the cpuN directories, so a host-count leak on any one surface makes the
// guest observe two different machine sizes at once.
void testGuestCpuTopology() {
    if (KSystem::guestCpuCount() != 1) {
        testFail("guest CPU topology must expose exactly one vCPU, got %u", KSystem::guestCpuCount());
        return;
    }
    if (KSystem::guestCpuList() != B("0")) {
        testFail("guest CPU list must be \"0\", got \"%s\"", KSystem::guestCpuList().c_str());
        return;
    }
    // The open functions require a real FsNode (FsOpenNode registers itself
    // with it), so hand them a throwaway directory node.
    static std::shared_ptr<FsNode> dummyNode =
        std::make_shared<FsFileNode>(1, 0, B("/topology-test"), B(""), B("/tmp"), true, true, nullptr);
    auto readAll = [](FsOpenNode* node) {
        std::string result;
        if (!node) {
            return result;
        }
        U8 buffer[16384];
        U32 read = node->readNative(buffer, sizeof(buffer));
        result.assign((const char*)buffer, read);
        delete node;
        return result;
    };
    const std::string online = readAll(openSysCpuOnline(dummyNode, 0, 0));
    if (online != "0") {
        testFail("/sys/devices/system/cpu/online must read \"0\", got \"%s\"", online.c_str());
        return;
    }
    const std::string cpuinfo = readAll(openCpuInfo(dummyNode, 0, 0));
    U32 processors = 0;
    for (size_t at = cpuinfo.find("processor\t:"); at != std::string::npos; at = cpuinfo.find("processor\t:", at + 1)) {
        processors++;
    }
    if (processors != KSystem::guestCpuCount()) {
        testFail("/proc/cpuinfo must list %u processor(s), found %u", KSystem::guestCpuCount(), processors);
        return;
    }
    if (cpuinfo.find("apicid\t\t: 0\n") == std::string::npos) {
        testFail("/proc/cpuinfo processor 0 must report apicid 0");
        return;
    }
    const std::string stat = readAll(openProcStat(dummyNode, 0, 0));
    if (!stat.starts_with("cpu ") || stat.find("\ncpu0 ") == std::string::npos ||
        stat.find("\ncpu1") != std::string::npos) {
        testFail("/proc/stat must contain only the aggregate and cpu0 lines");
        return;
    }
    // sysinfo(2) and /proc/meminfo draw from one source
    // (KSystem::guestRamInfo): 1 GiB total, free = total minus what live
    // processes have committed.
    U64 totalBytes = 0, freeBytes = 0;
    KSystem::guestRamInfo(totalBytes, freeBytes);
    char expectedTotal[64], expectedFree[64];
    snprintf(expectedTotal, sizeof(expectedTotal), "MemTotal: %llu kB", (unsigned long long)(totalBytes / 1024));
    snprintf(expectedFree, sizeof(expectedFree), "MemFree: %llu kB", (unsigned long long)(freeBytes / 1024));
    const std::string meminfo = readAll(openMemInfo(dummyNode, 0, 0));
    if (totalBytes != 1024ULL * 1024 * 1024 ||
        meminfo.find(expectedTotal) == std::string::npos ||
        meminfo.find(expectedFree) == std::string::npos) {
        testFail("/proc/meminfo totals must agree with KSystem::guestRamInfo, got \"%s\"", meminfo.c_str());
        return;
    }
    // Source guard: none of the guest-visible topology files may consult
    // the host CPU count again.
    std::string testPath = __FILE__;
    const size_t markerAt = testPath.find("/source/test/");
    if (markerAt == std::string::npos) {
        testFail("guest topology guard cannot locate source tree from %s", testPath.c_str());
        return;
    }
    const std::string root = testPath.substr(0, markerAt);
    const char* guarded[] = {
        "/source/kernel/sys/sysfs.cpp",
        "/source/kernel/sys/cpuonline.cpp",
        "/source/kernel/sys/sysdevices.cpp",
        "/source/kernel/proc/proccpuinfo.cpp",
    };
    for (const char* relative : guarded) {
        std::ifstream in(root + relative);
        if (!in) {
            testFail("guest topology guard cannot read %s", relative);
            return;
        }
        const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (source.find("Platform::getCpuCount()") != std::string::npos) {
            testFail("%s must not derive guest topology from the host CPU count", relative);
            return;
        }
    }
}

#endif
