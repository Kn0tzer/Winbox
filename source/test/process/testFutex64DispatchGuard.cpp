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

#include <fstream>
#include <string>
#include <iterator>

#include "../cpu/testCPU.h"

static bool readSourceBesideTest(const char* testFile, const char* relativePath, std::string& text) {
    std::string path = testFile;
    const std::string marker = "/source/test/";
    const size_t markerAt = path.find(marker);
    if (markerAt == std::string::npos) return false;
    path = path.substr(0, markerAt) + relativePath;
    std::ifstream file(path);
    if (!file) return false;
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

void testFutex64DispatchGuard() {
    std::string threadSource;
    std::string syscallSource;
    if (!readSourceBesideTest(__FILE__, "/source/kernel/kthread.cpp", threadSource) ||
        !readSourceBesideTest(__FILE__, "/source/kernel/syscall64.cpp", syscallSource)) {
        testFail("futex64 guard could not read its implementation sources");
        return;
    }
    const char* requiredThreadPaths[] = {
        "else if (cmd == FUTEX_REQUEUE || cmd == FUTEX_CMP_REQUEUE) {",
        "else if (cmd == FUTEX_CMP_REQUEUE_PI) {",
        "else if (cmd == FUTEX_WAKE_OP) {",
        "cmd == FUTEX_LOCK_PI || cmd == FUTEX_TRYLOCK_PI",
        "cmd == FUTEX_UNLOCK_PI",
        "cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET || cmd == FUTEX_WAIT_REQUEUE_PI",
        "ownerTid == tid) return -K_EDEADLK",
        "return -K_EPERM",
        "return unsupportedFutexOperation64(op, cmd)"
    };
    for (const char* path : requiredThreadPaths) {
        if (threadSource.find(path) == std::string::npos) {
            testFail("futex64 implementation lost required operation/error handling: %s", path);
        }
    }
    if (syscallSource.find("a4, a5, (U32)a6") == std::string::npos ||
        syscallSource.find("futex64(uaddr, op, val, timeoutAddr, uaddr2, val3)") == std::string::npos) {
        testFail("x64 futex syscall must forward arguments four through six without ABI shifts");
    }
    const size_t fallbackAt = syscallSource.find("// Threadless fallback for --x64-run-elf");
    const size_t fallbackEnd = syscallSource.find("// rt_sigtimedwait", fallbackAt);
    if (fallbackAt == std::string::npos || fallbackEnd == std::string::npos) {
        testFail("x64 futex threadless fallback boundary could not be located");
    } else {
        const std::string fallback = syscallSource.substr(fallbackAt, fallbackEnd - fallbackAt);
        if (fallback.find("unsupported threadless futex operation") == std::string::npos ||
            fallback.find("return (U64)-K_ENOSYS") == std::string::npos ||
            fallback.find("case X64_FUTEX_WAKE_OP:\n            return 0") != std::string::npos) {
            testFail("threadless futex operations must not silently claim unsupported work succeeded");
        }
    }
}

#endif
