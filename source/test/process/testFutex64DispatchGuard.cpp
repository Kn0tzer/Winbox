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
#ifdef BOXEDWINE_GUEST_X64
#include <atomic>
#include <thread>
#include <chrono>
#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#endif

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

#ifdef BOXEDWINE_GUEST_X64
void testFutex64Operations() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x76000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT)) memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3);
    const U64 word = page + 0x100;
    const U64 other = page + 0x104;
    const U64 timeout = page + 0x180;
    CPU64 caller(memory);
    caller.thread = context.thread;
    auto call = [](CPU64& cpu, U64 addr, U64 op, U64 value, U64 arg4 = 0, U64 addr2 = 0, U64 val3 = 0) {
        cpu.reg[X64_RAX].setU64(202);
        cpu.reg[X64_RDI].setU64(addr);
        cpu.reg[X64_RSI].setU64(op);
        cpu.reg[X64_RDX].setU64(value);
        cpu.reg[X64_R10].setU64(arg4);
        cpu.reg[X64_R8].setU64(addr2);
        cpu.reg[X64_R9].setU64(val3);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    constexpr U64 PRIVATE = 128;
    memory->writed(word, 0);
    if (call(caller, word, 6 | PRIVATE, 0) != 0 || memory->readd(word) != (context.thread->id & 0x3fffffff)) {
        testFail("FUTEX_LOCK_PI did not claim an unlocked word");
    }
    if (call(caller, word, 8 | PRIVATE, 0) != -35) testFail("same-owner FUTEX_TRYLOCK_PI did not return EDEADLK");
    KThread* worker = process->createThread();
    CPU64 workerCpu(memory);
    workerCpu.thread = worker;
    if (call(workerCpu, word, 8 | PRIVATE, 0) != -K_EAGAIN ||
        call(workerCpu, word, 7 | PRIVATE, 0) != -K_EPERM) {
        testFail("contended TRYLOCK_PI or non-owner UNLOCK_PI errno was incorrect");
    }
    if (call(caller, word, 7 | PRIVATE, 0) != 0 || memory->readd(word) != 0) {
        testFail("owner FUTEX_UNLOCK_PI did not release the word");
    }

    // Exercise owner handoff while the contending guest thread is blocked in
    // the actual syscall dispatcher on a native host thread.
    if (call(caller, word, 6 | PRIVATE, 0) != 0) testFail("FUTEX_LOCK_PI setup for handoff failed");
    std::atomic<bool> piStarted{false};
    S64 piResult = -999;
    worker->cpu64 = &workerCpu;
    std::thread piWaiter([&]() {
        KThread::setCurrentThread(worker);
        piStarted.store(true, std::memory_order_release);
        piResult = call(workerCpu, word, 6 | PRIVATE, 0);
    });
    while (!piStarted.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = 0; i < 100 && !(memory->readd(word) & 0x80000000u); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const S64 unlockResult = call(caller, word, 7 | PRIVATE, 0);
    piWaiter.join();
    if (unlockResult != 0 || piResult != 0 || call(workerCpu, word, 7 | PRIVATE, 0) != 0) {
        testFail("contended PI lock did not hand ownership to its waiter");
    }
    worker->cpu64 = nullptr;

    memory->writed(word, 9);
    memory->writed(other, 0);
    if (call(caller, word, 4 | PRIVATE, 0, 0, other, 8) != -K_EAGAIN ||
        call(caller, word, 4 | PRIVATE, 0, 0, other, 9) != 0 ||
        call(caller, word, 3 | PRIVATE, 0, 0, other) != 0) {
        testFail("CMP_REQUEUE comparison or REQUEUE return semantics failed");
    }
    const U32 wakeOpSetOne = 1U << 12; // FUTEX_OP_SET(1), FUTEX_OP_CMP_EQ(0)
    if (call(caller, word, 5 | PRIVATE, 1, 1, other, wakeOpSetOne) != 0 || memory->readd(other) != 1) {
        testFail("FUTEX_WAKE_OP did not apply its atomic SET operation");
    }
    memory->writed(word, 7);
    memory->writeq(timeout, 0);
    memory->writeq(timeout + 8, 0);
    if (call(caller, word, 9 | PRIVATE, 7, timeout, 0, 0xffffffff) != -K_ETIMEDOUT ||
        call(caller, word, 10 | PRIVATE, 1, 0, 0, 0) != -K_EINVAL ||
        call(caller, word, 10 | PRIVATE, 1, 0, 0, 0xffffffff) != 0) {
        testFail("WAIT_BITSET timeout or WAKE_BITSET mask validation failed");
    }
    memory->writed(word, 3);
    if (call(caller, word, 11 | PRIVATE, 3, timeout, other, 0xffffffff) != -K_ETIMEDOUT ||
        call(caller, word, 12 | PRIVATE, 1, 0, other, 2) != -K_EAGAIN ||
        call(caller, word, 12 | PRIVATE, 1, 0, other, 3) != 0 ||
        call(caller, word, 12 | PRIVATE, 2, 0, other, 3) != -K_EINVAL) {
        testFail("WAIT_REQUEUE_PI/CMP_REQUEUE_PI timeout, compare, or count semantics failed");
    }

    // Race a real host thread entering FUTEX_WAIT against a wake issued from
    // this thread. The implementation must either wake the registered waiter
    // or preserve the early wake so the waiter returns EWOULDBLOCK.
    const U64 raceWord = word + 0x20;
    memory->writed(raceWord, 11);
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    S64 waitResult = -999;
    std::thread hostWaiter([&]() {
        KThread::setCurrentThread(worker);
        worker->cpu64 = &workerCpu;
        started.store(true, std::memory_order_release);
        waitResult = call(workerCpu, raceWord, 0 | PRIVATE, 11);
        done.store(true, std::memory_order_release);
    });
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    S64 wakeResult = 0;
    for (int i = 0; i < 100 && !done.load(std::memory_order_acquire); ++i) {
        wakeResult = call(caller, raceWord, 1 | PRIVATE, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    hostWaiter.join();
    worker->cpu64 = nullptr;
    if (!done.load() || !((wakeResult == 1 && waitResult == 0) || (waitResult == -K_EWOULDBLOCK))) {
        testFail("FUTEX_WAIT/WAKE race lost a wake or returned an unexpected result (wake=%lld wait=%lld)",
                 (long long)wakeResult, (long long)waitResult);
    }
    const U64 requeueWord = word + 0x40;
    const U64 requeueOther = word + 0x44;
    auto requeueWaiter = [&](bool compare) {
        memory->writed(requeueWord, 23);
        memory->writed(requeueOther, 0);
        std::atomic<bool> waitStarted{false};
        S64 result = -999;
        std::thread waiter([&]() {
            KThread::setCurrentThread(worker);
            worker->cpu64 = &workerCpu;
            waitStarted.store(true, std::memory_order_release);
            result = call(workerCpu, requeueWord, PRIVATE, 23);
        });
        while (!waitStarted.load(std::memory_order_acquire)) std::this_thread::yield();
        S64 requeued = 0;
        for (int i = 0; i < 100 && requeued == 0; ++i) {
            requeued = call(caller, requeueWord, (compare ? 4 : 3) | PRIVATE, 0, 1, requeueOther, 23);
            if (!requeued) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const S64 woke = call(caller, requeueOther, 1 | PRIVATE, 1);
        waiter.join();
        worker->cpu64 = nullptr;
        if (requeued != 1 || woke != 1 || result != 0) {
            testFail("%s did not move and wake one blocked waiter (requeued=%lld wake=%lld wait=%lld)",
                     compare ? "CMP_REQUEUE" : "REQUEUE", (long long)requeued, (long long)woke,
                     (long long)result);
        }
    };
    requeueWaiter(false);
    requeueWaiter(true);
    process->removeThread(worker);
    delete worker;
}
#endif

#endif
