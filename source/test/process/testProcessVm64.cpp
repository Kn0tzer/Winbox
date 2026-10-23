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
#include "kepoll.h"
#include "kevent.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

void testEpollPwait2Syscall() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x74000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) && memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("epoll_pwait2 test could not map x64 buffers");
        return;
    }
    auto event = std::make_shared<KEvent>();
    event->counter = 1;
    KFileDescriptorPtr eventFd = process->allocFileDescriptor(event, K_O_RDWR, 0, -1, 0);
    auto epoll = std::make_shared<KEPoll>();
    KFileDescriptorPtr epollFd = process->allocFileDescriptor(epoll, K_O_RDWR, 0, -1, 0);
    constexpr U32 registration = TEST_HEAP_ADDRESS + 0x100;
    context.memory->writed(registration, 1);
    context.memory->writeq(registration + 4, 0xCAFEBABE12345678ULL);
    if ((S32)epoll->ctl(context.memory, 1, (FD)eventFd->handle, registration) != 0) {
        testFail("epoll_pwait2 test registration failed");
        process->close(epollFd->handle); process->close(eventFd->handle);
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto syscall = [&](U64 timeout, U64 mask, U64 maskSize) {
        cpu.reg[X64_RAX].setU64(441);
        cpu.reg[X64_RDI].setU64(epollFd->handle);
        cpu.reg[X64_RSI].setU64(page + 0x100);
        cpu.reg[X64_RDX].setU64(1);
        cpu.reg[X64_R10].setU64(timeout);
        cpu.reg[X64_R8].setU64(mask);
        cpu.reg[X64_R9].setU64(maskSize);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    memory->writeq(page, 0);
    memory->writeq(page + 8, 0);
    if (syscall(page, 0, 0) != 1 || memory->readd(page + 0x100) != 1 ||
        memory->readq(page + 0x104) != 0xCAFEBABE12345678ULL) {
        testFail("epoll_pwait2 did not return its ready event and 64-bit payload");
    }
    memory->writeq(page + 0x20, 0);
    memory->writeq(page + 0x28, 1000000000);
    if (syscall(page + 0x20, 0, 0) != -K_EINVAL || syscall(page, page + 0x40, 4) != -K_EINVAL) {
        testFail("epoll_pwait2 did not reject invalid timespec or sigset size");
    }
    process->close(epollFd->handle);
    process->close(eventFd->handle);
}

void testProcessVm64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x71000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("process_vm test could not map guest buffer");
        return;
    }
    const U64 localIov = page + 0x100;
    const U64 remoteIov = page + 0x120;
    const U64 localBuffer = page + 0x200;
    const U64 remoteBuffer = page + 0x300;
    const U64 pid = process->id;
    memory->memcpyToGuest(remoteBuffer, "remote-data", 11);
    memory->writeq(localIov, localBuffer);
    memory->writeq(localIov + 8, 11);
    memory->writeq(remoteIov, remoteBuffer);
    memory->writeq(remoteIov + 8, 11);

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto syscall = [&](U64 number, U64 localCount, U64 flags, U64 remoteCount = 1) {
        cpu.reg[X64_RAX].setU64(number);
        cpu.reg[X64_RDI].setU64(pid);
        cpu.reg[X64_RSI].setU64(localIov);
        cpu.reg[X64_RDX].setU64(localCount);
        cpu.reg[X64_R10].setU64(remoteIov);
        cpu.reg[X64_R8].setU64(remoteCount);
        cpu.reg[X64_R9].setU64(flags);
        ksyscall64(&cpu);
        return cpu.reg[X64_RAX].u64;
    };
    U64 result = syscall(310, 1, 0);
    char copied[11]{};
    memory->memcpyFromGuest(copied, localBuffer, sizeof(copied));
    if (result != 11 || std::memcmp(copied, "remote-data", 11) != 0) {
        testFail("process_vm_readv returned %lld or copied incorrect bytes", (long long)(S64)result);
    }

    memory->memcpyToGuest(localBuffer, "local-write", 11);
    result = syscall(311, 1, 0);
    std::memset(copied, 0, sizeof(copied));
    memory->memcpyFromGuest(copied, remoteBuffer, sizeof(copied));
    if (result != 11 || std::memcmp(copied, "local-write", 11) != 0) {
        testFail("process_vm_writev returned %lld or copied incorrect bytes", (long long)(S64)result);
    }
    if ((S64)syscall(310, 1, 1) != -K_EINVAL || (S64)syscall(310, 1025, 0) != -K_EINVAL) {
        testFail("process_vm_readv did not validate flags/iovec count");
    }
    // Linux returns the bytes completed before a later iovec faults. The first
    // pair is valid and the second pair points into an unmapped page.
    memory->writeq(localIov + 16, 0x73000000ULL);
    memory->writeq(localIov + 24, 1);
    memory->writeq(remoteIov + 16, 0x73001000ULL);
    memory->writeq(remoteIov + 24, 1);
    if (syscall(310, 2, 0, 2) != 11) {
        testFail("process_vm_readv did not return the valid partial-copy byte count");
    }
    memory->mprotect(page, K64_PAGE_SIZE, 1);
    if ((S64)syscall(310, 1, 0) != -K_EFAULT) {
        testFail("process_vm_readv accepted a read-only local destination");
    }
    memory->mprotect(page, K64_PAGE_SIZE, 3);
}

void testPidfd64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x72000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT)) memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3);
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto syscall = [&](U64 number, U64 arg1, U64 arg2, U64 arg3, U64 arg4) {
        cpu.reg[X64_RAX].setU64(number);
        cpu.reg[X64_RDI].setU64(arg1);
        cpu.reg[X64_RSI].setU64(arg2);
        cpu.reg[X64_RDX].setU64(arg3);
        cpu.reg[X64_R10].setU64(arg4);
        ksyscall64(&cpu);
        return cpu.reg[X64_RAX].u64;
    };
    U64 fd = syscall(434, process->id, 0x80000, 0, 0);
    if ((S64)fd < 0) {
        testFail("pidfd_open(self) returned %lld", (long long)(S64)fd);
        return;
    }
    KFileDescriptorPtr descriptor = process->getFileDescriptor((FD)fd);
    if (!descriptor || !(descriptor->descriptorFlags & FD_CLOEXEC) ||
        (S64)syscall(424, fd, 0, 0, 0) != 0 || (S64)syscall(434, process->id, 4, 0, 0) != -K_EINVAL ||
        (S64)syscall(424, fd, 65, 0, 0) != -K_EINVAL) {
        testFail("pidfd flags, signal-zero probe, or signal validation failed");
    }
    if ((S64)syscall(424, fd, 0, page, 0) != -K_EOPNOTSUPP) {
        testFail("pidfd_send_signal accepted unsupported siginfo delivery without a warning errno");
    }
    process->close((FD)fd);
}

#endif
#endif
