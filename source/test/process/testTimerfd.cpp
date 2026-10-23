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

#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"
#ifdef BOXEDWINE_GUEST_X64
#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#endif

void testTimerfdCore() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }

    if ((S32)process->timerfd_create(2, 0) != -K_EINVAL ||
        (S32)process->timerfd_create(0, 0x40000000) != -K_EINVAL) {
        testFail("timerfd_create accepted an invalid clock or flag");
        return;
    }
    U32 fd = process->timerfd_create(1, K_O_NONBLOCK | K_O_CLOEXEC);
    if ((S32)fd < 0) {
        testFail("timerfd_create(CLOCK_MONOTONIC) failed: %d", (S32)fd);
        return;
    }
    KFileDescriptorPtr descriptor = process->getFileDescriptor(fd);
    std::shared_ptr<KTimer> timer = descriptor ? std::dynamic_pointer_cast<KTimer>(descriptor->kobject) : nullptr;
    if (!timer || !(descriptor->accessFlags & K_O_NONBLOCK) || !(descriptor->descriptorFlags & FD_CLOEXEC)) {
        testFail("timerfd_create did not preserve descriptor flags or timer type");
        process->close(fd);
        return;
    }

    // A deadline already in the past produces one readable expiration. Reads
    // must consume the accumulated count exactly once and require an 8-byte
    // timerfd payload.
    timer->setTimes(0, timer->getClockNowMicroseconds() - 1000);
    U64 expirations = 0;
    U32 result = timer->readNative((U8*)&expirations, sizeof(expirations));
    if (result != 8 || expirations != 1) {
        testFail("timerfd read returned %u bytes/count %llu, expected 8/1", result,
                 (unsigned long long)expirations);
    }
    result = timer->readNative((U8*)&expirations, sizeof(expirations));
    if ((S32)result != -K_EAGAIN) {
        testFail("nonblocking timerfd repeated an already-consumed expiration: %d", (S32)result);
    }
    if ((S32)timer->readNative((U8*)&expirations, 4) != -K_EINVAL) {
        testFail("timerfd accepted a read size other than 8 bytes");
    }

    timer->setTimes(0, 0); // disarming a timer must not invent an expiration
    result = timer->readNative((U8*)&expirations, sizeof(expirations));
    if ((S32)result != -K_EAGAIN) {
        testFail("disarmed timerfd produced a spurious expiration");
    }
    process->close(fd);
}

#ifdef BOXEDWINE_GUEST_X64
void testTimerfd64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    KMemory64* memory = process->memory64;
    constexpr U64 testPage = 0x70000000ULL;
    if (!memory->getPageFlags(testPage >> K_PAGE_SHIFT)) memory->mmapAnonymousFixed(testPage, 0x1000, 3);

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto syscall = [&](U64 nr, U64 a1 = 0, U64 a2 = 0, U64 a3 = 0, U64 a4 = 0) {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        ksyscall64(&cpu);
        return cpu.reg[X64_RAX].u64;
    };
    U64 invalid = syscall(283, 2, 0); // unsupported clock id
    if (invalid != (U64)-K_EINVAL) {
        testFail("x64 timerfd_create invalid clock returned %lld", (long long)invalid);
        return;
    }
    U64 result = syscall(283, 1, K_O_NONBLOCK | K_O_CLOEXEC);
    if ((S64)result < 0) {
        testFail("x64 timerfd_create(CLOCK_MONOTONIC) returned %lld", (long long)(S64)result);
        return;
    }
    const FD fd = (FD)result;
    KFileDescriptorPtr descriptor = process->getFileDescriptor(fd);
    std::shared_ptr<KTimer> timer = descriptor ? std::dynamic_pointer_cast<KTimer>(descriptor->kobject) : nullptr;
    if (!timer || !(descriptor->descriptorFlags & FD_CLOEXEC) || !(descriptor->accessFlags & K_O_NONBLOCK)) {
        testFail("x64 timerfd_create lost descriptor flags");
        process->close(fd);
        return;
    }

    // x86-64 itimerspec: two {s64 sec, s64 nsec} values.
    // CLOCK_MONOTONIC absolute deadlines use the monotonic epoch, not realtime.
    // KSystem's monotonic clock starts with the test process, so give it time
    // to pass the one-second absolute deadline before asking for readiness.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    memory->writeq(testPage, 0);
    memory->writeq(testPage + 8, 0);
    memory->writeq(testPage + 16, 1);
    memory->writeq(testPage + 24, 0);
    result = syscall(286, fd, 1 /*TFD_TIMER_ABSTIME*/, testPage, testPage + 0x40);
    if (result != 0 || !timer->isReadReady()) {
        testFail("x64 timerfd_settime absolute past deadline returned %lld/read-ready=%d",
                 (long long)(S64)result, timer->isReadReady());
        process->close(fd);
        return;
    }
    result = syscall(287, fd, testPage + 0x80);
    if (result != 0 || memory->readq(testPage + 0x80) != 0 || memory->readq(testPage + 0x88) != 0) {
        testFail("x64 timerfd_gettime returned %lld for expired one-shot", (long long)(S64)result);
    }
    U64 expirations = 0;
    if (timer->readNative((U8*)&expirations, 8) != 8 || expirations != 1 || timer->isReadReady()) {
        testFail("x64 timerfd expiration read did not consume exactly one expiration");
    }
    memory->writeq(testPage + 16, 0);
    result = syscall(286, fd, 0, testPage, 0);
    if (result != 0 || timer->isReadReady()) {
        testFail("x64 timerfd disarm created a spurious expiration");
    }
    result = syscall(286, fd, 2 /*unsupported CANCEL_ON_SET without ABS*/, testPage, 0);
    if (result != (U64)-K_EINVAL) testFail("x64 timerfd_settime accepted invalid flags");
    process->close(fd);
}

void testCopyFileRange64Syscall() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    KMemory64* memory = process->memory64;
    constexpr U64 testPage = 0x70000000ULL;
    if (!memory->getPageFlags(testPage >> K_PAGE_SHIFT)) memory->mmapAnonymousFixed(testPage, 0x1000, 3);
    const U32 inFd = process->memfd_create(B("copy-range-in"), 0);
    const U32 outFd = process->memfd_create(B("copy-range-out"), 0);
    if ((S32)inFd < 0 || (S32)outFd < 0) {
        testFail("copy_file_range test memfd setup failed");
        return;
    }
    KFileDescriptorPtr inDescriptor = process->getFileDescriptor(inFd);
    KFileDescriptorPtr outDescriptor = process->getFileDescriptor(outFd);
    auto input = std::dynamic_pointer_cast<KFile>(inDescriptor->kobject);
    auto output = std::dynamic_pointer_cast<KFile>(outDescriptor->kobject);
    const U8 source[] = {'c', 'o', 'p', 'y', '!'};
    if (!input || !output || input->writeNative((U8*)source, sizeof(source)) != sizeof(source)) {
        testFail("copy_file_range test input setup failed");
        process->close(inFd); process->close(outFd);
        return;
    }
    input->seek(0);

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto syscall = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1); cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3); cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5); cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return cpu.reg[X64_RAX].u64;
    };
    memory->writeq(testPage, 0);
    memory->writeq(testPage + 8, 0);
    U64 result = syscall(326, inFd, testPage, outFd, testPage + 8, sizeof(source), 0);
    U8 copied[sizeof(source)] = {};
    U32 read = output->preadNative(copied, 0, sizeof(copied));
    if (result != sizeof(source) || read != sizeof(source) || std::memcmp(source, copied, sizeof(source)) ||
        memory->readq(testPage) != sizeof(source) || memory->readq(testPage + 8) != sizeof(source)) {
        testFail("copy_file_range explicit-offset copy or offset update failed");
    }
    if (syscall(326, inFd, testPage, outFd, testPage + 8, 1, 1) != (U64)-K_EINVAL) {
        testFail("copy_file_range accepted unsupported flags");
    }
    if (syscall(326, 9999, 0, outFd, 0, 1, 0) != (U64)-K_EBADF) {
        testFail("copy_file_range invalid fd did not return EBADF");
    }
    result = syscall(326, inFd, 0, outFd, 0, 2, 0);
    if (result != 2 || input->getPos() != 2 || output->getPos() != 2) {
        testFail("copy_file_range current-offset copy did not advance file positions");
    }
    process->close(inFd); process->close(outFd);
}
#endif

#endif
