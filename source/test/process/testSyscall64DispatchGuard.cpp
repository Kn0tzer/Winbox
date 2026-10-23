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
#include "../../io/fsfilenode.h"
#ifdef BOXEDWINE_GUEST_X64
#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "ksignal.h"
#endif

// Keep a source-level guard around the x64 ABI dispatcher. The ordinary native
// test build does not enable BOXEDWINE_GUEST_X64, so it cannot call that
// dispatcher directly; deriving its path from __FILE__ lets this guard still
// inspect the exact code that will be compiled by the x64 target.
void testSyscall64DispatchGuard() {
    std::string testPath = __FILE__;
    const std::string marker = "/source/test/";
    const size_t markerAt = testPath.find(marker);
    if (markerAt == std::string::npos) {
        testFail("syscall64 dispatch guard cannot locate source tree from %s", testPath.c_str());
        return;
    }
    const std::string sourcePath = testPath.substr(0, markerAt) + "/source/kernel/syscall64.cpp";
    std::ifstream sourceFile(sourcePath);
    if (!sourceFile) {
        testFail("syscall64 dispatch guard cannot read %s", sourcePath.c_str());
        return;
    }
    const std::string source((std::istreambuf_iterator<char>(sourceFile)), std::istreambuf_iterator<char>());
    const size_t dispatchAt = source.find("void ksyscall64(");
    const size_t switchAt = source.find("switch (nr)", dispatchAt);
    if (dispatchAt == std::string::npos || switchAt == std::string::npos) {
        testFail("syscall64 dispatch guard cannot find dispatcher switch");
        return;
    }

    // Extract the switch body with brace depth so nested case scopes do not
    // confuse the assertions below.
    const size_t openAt = source.find('{', switchAt);
    size_t closeAt = openAt;
    int depth = 0;
    for (; closeAt < source.size(); ++closeAt) {
        if (source[closeAt] == '{') depth++;
        else if (source[closeAt] == '}' && --depth == 0) break;
    }
    if (openAt == std::string::npos || closeAt >= source.size()) {
        testFail("syscall64 dispatch guard cannot delimit dispatcher switch");
        return;
    }
    const std::string dispatch = source.substr(openAt, closeAt - openAt + 1);
    const std::string fallback = "default:\n            ret = unimplementedSyscall64(nr);\n            break;";
    if (dispatch.find(fallback) == std::string::npos) {
        testFail("x64 syscall default branch must use the central logged -ENOSYS path");
    }
    if (dispatch.find("ksyscall64: unimplemented syscall #%llu (%s)") != std::string::npos) {
        testFail("x64 syscall fallback logging must stay centralized");
    }
    const std::string helper = "static U64 unimplementedSyscall64(U64 nr)";
    const size_t helperAt = source.find(helper);
    const size_t helperEnd = source.find("#ifdef __EMSCRIPTEN__", helperAt);
    if (helperAt == std::string::npos || helperEnd == std::string::npos) {
        testFail("x64 syscall central ENOSYS helper is missing");
        return;
    }
    const std::string helperBody = source.substr(helperAt, helperEnd - helperAt);
    if (helperBody.find("reportedNumbers.insert((U32)nr).second") == std::string::npos ||
        helperBody.find("return (U64)-K_ENOSYS;") == std::string::npos ||
        helperBody.find("ksyscall64: unimplemented syscall #%llu (%s)") == std::string::npos) {
        testFail("x64 syscall fallback must log once per number and return -ENOSYS");
    }
    if (source.find("ksyscall64: pidfd_send_signal siginfo delivery unsupported; returned -EOPNOTSUPP") == std::string::npos) {
        testFail("unsupported pidfd siginfo delivery must log its feature and errno");
    }
}

#ifdef BOXEDWINE_GUEST_X64
void testInotify64LoudRejection() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    CPU64 cpu(context.process->memory64);
    cpu.thread = context.thread;
    for (U64 number : {253ULL, 254ULL, 255ULL, 294ULL}) {
        cpu.reg[X64_RAX].setU64(number);
        cpu.reg[X64_RDI].setU64(0);
        cpu.reg[X64_RSI].setU64(0);
        cpu.reg[X64_RDX].setU64(0);
        ksyscall64(&cpu);
        if ((S64)cpu.reg[X64_RAX].u64 != -K_ENOSYS) {
            testFail("inotify syscall %llu did not return -ENOSYS", (unsigned long long)number);
        }
    }
}

void testSignalfd64Syscalls() {
    TestContext& context = testContext();
    if (!context.process->fdNode) {
        context.process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x74000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT)) memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3);
    const U64 maskAddress = page + 0x100;
    memory->writeq(maskAddress, (1ULL << (K_SIGUSR1 - 1)) | (1ULL << (K_SIGKILL - 1)) |
                                (1ULL << (K_SIGSTOP - 1)));
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto call = [&](U64 number, S32 fd, U64 mask, U64 size, U64 flags) {
        cpu.reg[X64_RAX].setU64(number);
        cpu.reg[X64_RDI].setU64((U64)(S64)fd);
        cpu.reg[X64_RSI].setU64(mask);
        cpu.reg[X64_RDX].setU64(size);
        cpu.reg[X64_R10].setU64(flags);
        ksyscall64(&cpu);
        return cpu.reg[X64_RAX].u64;
    };
    U64 fd = call(289, -1, maskAddress, 8, K_O_NONBLOCK | K_O_CLOEXEC);
    if ((S64)fd < 0) { testFail("signalfd4 create returned %lld", (long long)(S64)fd); return; }
    KFileDescriptorPtr descriptor = context.process->getFileDescriptor((FD)fd);
    auto signal = descriptor ? std::dynamic_pointer_cast<KSignal>(descriptor->kobject) : nullptr;
    if (!signal || signal->mask != (1ULL << (K_SIGUSR1 - 1)) ||
        !(descriptor->accessFlags & K_O_NONBLOCK) || !(descriptor->descriptorFlags & FD_CLOEXEC)) {
        testFail("signalfd4 did not preserve the blockable mask and descriptor flags");
    }
    U8 record[128];
    std::memset(record, 0xA5, sizeof(record));
    KThread* previousThread = KThread::currentThread();
    KThread::setCurrentThread(context.thread);
    context.thread->pendingSignals |= 1ULL << (K_SIGUSR1 - 1);
    const U32 bytes = signal ? signal->readNative(record, sizeof(record)) : 0;
    KThread::setCurrentThread(previousThread);
    U32 deliveredSignal = 0;
    std::memcpy(&deliveredSignal, record, sizeof(deliveredSignal));
    if (bytes != sizeof(record) || deliveredSignal != K_SIGUSR1 ||
        (context.thread->pendingSignals & (1ULL << (K_SIGUSR1 - 1))) != 0) {
        testFail("signalfd read did not consume and encode the queued signal");
    }
    if (signal && signal->readNative(record, sizeof(record)) != (U32)-K_EAGAIN) {
        testFail("empty nonblocking signalfd read did not return EAGAIN");
    }
    memory->writeq(maskAddress, 0);
    if (call(282, (S32)fd, maskAddress, 8, 0) != fd || signal->mask != 0 ||
        (descriptor->accessFlags & K_O_NONBLOCK) || (descriptor->descriptorFlags & FD_CLOEXEC)) {
        testFail("signalfd update did not replace mask and flags");
    }
    if ((S64)call(289, -1, maskAddress, 4, 0) != -K_EINVAL ||
        (S64)call(289, -1, maskAddress, 8, 0x40000000) != -K_EINVAL) {
        testFail("signalfd4 accepted invalid sigset size or flags");
    }
    context.process->close((FD)fd);
}
#endif

#endif
