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

#include <string>

#include "../cpu/testCPU.h"

#ifndef BOXEDWINE_GUEST_X64

// /proc/<pid>/maps and smaps are generated from the live page tables, so a
// mapping created through the guest mmap path must show up with the right
// range and permissions, and disappear again after munmap.
void testProcMaps32() {
    TestContext& context = testContext();
    constexpr U32 addr = 0x33000000;
    U32 result = context.memory->mmap(context.thread, addr, K_PAGE_SIZE * 2,
                                      K_PROT_READ | K_PROT_WRITE,
                                      K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED_NOREPLACE, -1, 0);
    if (result != addr) {
        testFail("32-bit maps test could not map its fixture range");
        return;
    }
    const std::string maps = context.memory->generateMaps(false).c_str();
    const std::string smaps = context.memory->generateMaps(true).c_str();
    context.memory->unmap(addr, K_PAGE_SIZE * 2);
    const std::string after = context.memory->generateMaps(false).c_str();
    if (maps.find("33000000-33002000 rw-p") == std::string::npos) {
        testFail("32-bit /proc maps is missing the fixture range, got:\n%s", maps.c_str());
        return;
    }
    if (smaps.find("33000000-33002000 rw-p") == std::string::npos ||
        smaps.find("Size: 8 kB") == std::string::npos) {
        testFail("32-bit /proc smaps is missing the fixture range or its size");
        return;
    }
    if (after.find("33000000-33002000") != std::string::npos) {
        testFail("32-bit /proc maps still lists the fixture range after munmap");
        return;
    }
}

#else

#include "cpu64.h"
#include "kmemory64.h"

void testProcMaps64() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x76000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("x64 maps test could not map its fixture page");
        return;
    }
    const std::string maps = memory->generateMaps(false).c_str();
    const std::string smaps = memory->generateMaps(true).c_str();
    if (maps.find("000076000000-000076001000 rw-p") == std::string::npos) {
        testFail("x64 /proc maps is missing the fixture range, got:\n%s", maps.c_str());
        return;
    }
    if (smaps.find("Size: 4 kB") == std::string::npos || smaps.find("Rss: 0 kB") == std::string::npos) {
        testFail("x64 /proc smaps must report the reserved-but-untouched page as Size 4 kB, Rss 0 kB");
        return;
    }
}

#endif
#endif
