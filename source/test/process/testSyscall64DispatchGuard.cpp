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
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
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
    if (source.find("queueSiginfo64(targetThread") == std::string::npos ||
        source.find("cpu->reg[X64_RSI].setU64((sa.flags & 0x4 /*SA_SIGINFO*/) ? frameBase + X64_SIGINFO_OFF_IN_FRAME : 0)") == std::string::npos) {
        testFail("pidfd and queued signal paths must deliver siginfo to SA_SIGINFO handlers");
    }
    if (source.find("case 332: // statx") == std::string::npos ||
        source.find("ret = sys_statx64(cpu, a1, a2, a3, a4, a5);") == std::string::npos ||
        source.find("cpu->memory->writeq(statbuf + 112, seconds)") == std::string::npos ||
        source.find("cpu->memory->writeq(statbuf + 144, 1)") == std::string::npos) {
        testFail("statx syscall dispatch or x86-64 structure offsets changed unexpectedly");
    }

    const std::string root = testPath.substr(0, markerAt);
    std::ifstream randomFile(root + "/source/kernel/krandom.cpp");
    std::ifstream syscall32File(root + "/source/kernel/syscall.cpp");
    std::ifstream processFile(root + "/source/kernel/kprocess.cpp");
    std::ifstream unixSocketFile(root + "/source/kernel/kunixsocket.cpp");
    const std::string randomSource((std::istreambuf_iterator<char>(randomFile)), std::istreambuf_iterator<char>());
    const std::string syscall32((std::istreambuf_iterator<char>(syscall32File)), std::istreambuf_iterator<char>());
    const std::string processSource((std::istreambuf_iterator<char>(processFile)), std::istreambuf_iterator<char>());
    const std::string unixSocketSource((std::istreambuf_iterator<char>(unixSocketFile)), std::istreambuf_iterator<char>());
    if (!randomFile || !syscall32File || !processFile || !unixSocketFile || randomSource.find("std::random_device") == std::string::npos ||
        source.find("6364136223846793005") != std::string::npos ||
        syscall32.find("std::mt19937") != std::string::npos) {
        testFail("getrandom must use the shared nondeterministic source, not a deterministic PRNG");
    }
    if (source.find("sys_rangeHasPermission64(cpu->memory, bufAddr, buflen, K64_PAGE_WRITE)") == std::string::npos ||
        syscall32.find("cpu->memory->canWrite(buf, count)") == std::string::npos ||
        syscall32.find("if (bw64RecoverEnabled() && (ARG3 & ~7u))") != std::string::npos) {
        testFail("32-bit getrandom must validate flags and the full writable range on every build");
    }
    if (processSource.find("constexpr U64 CLONE_ARGS_V0_SIZE = 64") == std::string::npos ||
        processSource.find("if (size > K_PAGE_SIZE) return (U64)-K_E2BIG;") == std::string::npos ||
        processSource.find("if (this->memory64->readb(argsAddr + offset) != 0) return (U64)-K_E2BIG;") == std::string::npos ||
        processSource.find("clone64(thread, flags, stack + stack_size, parent_tid, tls, child_tid, exit_signal)") == std::string::npos ||
        processSource.find("newProcess->childExitSignal64 = (U32)exitSignal;") == std::string::npos ||
        processSource.find("createPidFdForClone64(thread, (U32)result)") == std::string::npos) {
        testFail("clone3 must validate versioned struct size, readable memory, and unknown extension bytes");
    }
    if (unixSocketSource.find("if (name == K_SO_ATTACH_FILTER)") == std::string::npos ||
        unixSocketSource.find("if (name == K_SO_DETACH_FILTER)") == std::string::npos ||
        unixSocketSource.find("con->applySocketFilter(packet.data()") == std::string::npos) {
        testFail("Unix socket filters must validate, attach/detach, and run on queued messages");
    }
}

#ifdef BOXEDWINE_GUEST_X64
void testInotify64Syscalls() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x77000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("x64 inotify test could not map its fixture page");
        return;
    }
    constexpr U64 pathAt = page + 0x100;
    memory->memcpyToGuest(pathAt, "/", 2);

    // The watch target must resolve: install the statx-style root fixture.
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), B("/tmp"), true, true, nullptr);
    Fs::clearPathCache();

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 nr, U64 a1, U64 a2, U64 a3) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    // __NR_inotify_init1 = 294: unknown flag bits are rejected.
    if (invoke(294, 0x40000000ULL, 0, 0) != -K_EINVAL) {
        testFail("x64 inotify_init1 accepted unknown flags");
    }
    // IN_NONBLOCK (0x800) | IN_CLOEXEC (0x80000)
    S64 fd = invoke(294, 0x80800ULL, 0, 0);
    if (fd < 0) {
        testFail("x64 inotify_init1 failed: %lld", (long long)fd);
        return;
    }
    // __NR_inotify_init = 253 also works.
    S64 fd2 = invoke(253, 0, 0, 0);
    if (fd2 < 0) {
        testFail("x64 inotify_init failed: %lld", (long long)fd2);
    }
    // __NR_inotify_add_watch = 254: bad fd, then a real watch on "/".
    if (invoke(254, 9999, pathAt, 0xfffULL) != -K_EBADF) {
        testFail("x64 inotify_add_watch on a bad fd was not -EBADF");
    }
    S64 wd = invoke(254, (U64)fd, pathAt, 0xfffULL);
    if (wd < 1) {
        testFail("x64 inotify_add_watch(/) failed: %lld", (long long)wd);
        return;
    }
    // Re-adding merges and returns the same wd.
    if (invoke(254, (U64)fd, pathAt, 0x4ULL) != wd) {
        testFail("x64 inotify_add_watch did not merge an existing watch");
    }
    // __NR_inotify_rm_watch = 255.
    if (invoke(255, (U64)fd, (U64)wd, 0) != 0) {
        testFail("x64 inotify_rm_watch failed");
    }
    if (invoke(255, (U64)fd, (U64)wd, 0) != -K_EINVAL) {
        testFail("x64 inotify_rm_watch of a removed watch was not -EINVAL");
    }
    invoke(3, (U64)fd, 0, 0);  // close
    if (fd2 >= 0) invoke(3, (U64)fd2, 0, 0);
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
