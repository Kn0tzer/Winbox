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

#include "testCPU.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#if defined(__linux__) && defined(BOXEDWINE_POSIX) && !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

extern void failed(const char* msg, ...);

namespace {

constexpr U32 TEST_PAGES_PER_SEG = 32;

std::mutex testContextCreateMutex;
std::unique_ptr<TestContext> serialContext;
std::vector<std::unique_ptr<TestContext>> parallelContexts;
thread_local TestContext* currentContext = nullptr;
thread_local bool runningParallelTest = false;

void setupSegments(TestContext& context) {
    CPU* cpu = context.cpu;
    KProcess* process = context.process.get();

    for (int i = 0; i < 6; i++) {
        cpu->seg[i].address = 0;
        cpu->seg[i].value = 0;
        process->hasSetSeg[i] = false;
    }
    process->hasSetStackMask = false;

    struct user_desc* ldt = process->getLDT(TEST_HEAP_SEG >> 3);
    ldt->entry_number = TEST_HEAP_SEG >> 3;
    ldt->base_addr = TEST_HEAP_ADDRESS;
    ldt->seg_32bit = 1;
    ldt->read_exec_only = 0;
    ldt->seg_not_present = 0;

    ldt = process->getLDT(TEST_STACK_SEG >> 3);
    ldt->entry_number = TEST_STACK_SEG >> 3;
    ldt->base_addr = TEST_STACK_ADDRESS - K_PAGE_SIZE * TEST_PAGES_PER_SEG;
    ldt->seg_32bit = 1;
    ldt->read_exec_only = 0;
    ldt->seg_not_present = 0;

    ldt = process->getLDT(TEST_CODE_SEG >> 3);
    ldt->entry_number = TEST_CODE_SEG >> 3;
    ldt->base_addr = TEST_CODE_ADDRESS;
    ldt->seg_32bit = 1;
    ldt->read_exec_only = 0;
    ldt->seg_not_present = 0;

    ldt = process->getLDT(TEST_CODE_SEG_16 >> 3);
    ldt->entry_number = TEST_CODE_SEG_16 >> 3;
    ldt->base_addr = TEST_CODE_ADDRESS;
    ldt->seg_32bit = 0;
    ldt->read_exec_only = 0;
    ldt->seg_not_present = 0;

    cpu->seg[CS].address = TEST_CODE_ADDRESS;
    cpu->seg[CS].value = TEST_CODE_SEG;
    cpu->seg[DS].address = TEST_HEAP_ADDRESS;
    cpu->seg[DS].value = TEST_HEAP_SEG;
    cpu->seg[SS].address = TEST_STACK_ADDRESS - K_PAGE_SIZE * TEST_PAGES_PER_SEG;
    cpu->seg[SS].value = TEST_STACK_SEG;
    cpu->stackMask = 0xffffffff;
    cpu->stackNotMask = 0;
    cpu->seg[ES].address = 0;
    cpu->seg[ES].value = 0;
    cpu->seg[GS].address = 0;
    cpu->seg[GS].value = 0;
    cpu->seg[FS].address = 0;
    cpu->seg[FS].value = 0;
    process->hasSetSeg[CS] = true;
    process->hasSetSeg[DS] = true;
    process->hasSetSeg[SS] = true;
}

void resetMemory(TestContext& context) {
    context.memory->memset(TEST_CODE_ADDRESS, 0, K_PAGE_SIZE * TEST_PAGES_PER_SEG);
    context.memory->memset(TEST_STACK_ADDRESS - K_PAGE_SIZE * TEST_PAGES_PER_SEG, 0, K_PAGE_SIZE * TEST_PAGES_PER_SEG);
    context.memory->memset(TEST_HEAP_ADDRESS, 0, K_PAGE_SIZE * TEST_PAGES_PER_SEG);
}

void createContext(TestContext& context) {
    context.process = KProcess::create();
    context.memory = KMemory::create(context.process.get());
    context.process->memory = context.memory;
    context.thread = context.process->createThread();
    context.cpu = context.thread->cpu;
    KThread::setCurrentThread(context.thread);

    context.memory->mmap(context.thread, ((TEST_STACK_ADDRESS >> K_PAGE_SHIFT) - TEST_PAGES_PER_SEG) << K_PAGE_SHIFT, TEST_PAGES_PER_SEG << K_PAGE_SHIFT, K_PROT_WRITE | K_PROT_READ | K_PROT_READ, K_MAP_FIXED | K_MAP_PRIVATE, -1, 0);
    context.memory->mmap(context.thread, TEST_CODE_ADDRESS, TEST_PAGES_PER_SEG << K_PAGE_SHIFT, K_PROT_WRITE | K_PROT_READ | K_PROT_READ | K_PROT_EXEC, K_MAP_FIXED | K_MAP_PRIVATE, -1, 0);
    context.memory->mmap(context.thread, TEST_HEAP_ADDRESS, TEST_PAGES_PER_SEG << K_PAGE_SHIFT, K_PROT_READ | K_PROT_WRITE, K_MAP_FIXED | K_MAP_PRIVATE, -1, 0);
    setupSegments(context);
    resetMemory(context);
}

} // namespace

TestContext& testContext() {
    if (!currentContext) {
        std::lock_guard<std::mutex> lock(testContextCreateMutex);
        if (!serialContext) {
            serialContext = std::make_unique<TestContext>();
            createContext(*serialContext);
        }
        currentContext = serialContext.get();
    }
    KThread::setCurrentThread(currentContext->thread);
    return *currentContext;
}

void ensureParallelContexts(U32 workerCount) {
    std::lock_guard<std::mutex> lock(testContextCreateMutex);
    while (parallelContexts.size() < workerCount) {
        std::unique_ptr<TestContext> context = std::make_unique<TestContext>();
        createContext(*context);
        parallelContexts.push_back(std::move(context));
    }
}

void bindParallelContext(U32 index) {
    currentContext = parallelContexts[index].get();
    KThread::setCurrentThread(currentContext->thread);
}

void testNewInstruction(int flags) {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;

    setupSegments(context);
    resetMemory(context);
    context.codeIp = TEST_CODE_ADDRESS;
    cpu->lazyFlagType = FLAGS_NONE;
    cpu->setFlags(flags, FMASK_ALL);
    cpu->big = 1;
    cpu->reg[0].u32 = 0;
    cpu->reg[1].u32 = 0;
    cpu->reg[2].u32 = 0;
    cpu->reg[3].u32 = 0;
    cpu->reg[4].u32 = 4096;
    cpu->reg[5].u32 = 0;
    cpu->reg[6].u32 = 0;
    cpu->reg[7].u32 = 0;
    cpu->eip.u32 = 0;
    context.memory->clearOpCache();
    cpu->mxcsr = 0x1F80;
}

void testPushCode8(int value) {
    TestContext& context = testContext();
    context.memory->writeb(context.codeIp, value);
    context.codeIp++;
}

void testPushCode16(int value) {
    TestContext& context = testContext();
    context.memory->writew(context.codeIp, value);
    context.codeIp += 2;
}

void testPushCode32(int value) {
    TestContext& context = testContext();
    context.memory->writed(context.codeIp, value);
    context.codeIp += 4;
}

void testRunCPU() {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;

    testPushCode8(0xcd);
    testPushCode8(0x97);

    cpu->nextOp = cpu->getNextOp();
    do {
        cpu->run();
    } while (!cpu->nextOp || cpu->nextOp->inst != TestEnd);
}

#ifdef BOXEDWINE_WASM_JIT
// Headless 32-bit CPU tests do not link the SDL input observer.
#ifndef BOXEDWINE_GUEST_X64
void xwireRecordInputDelivery(uint8_t, uint32_t) {}
#endif

// An exiting syscall can destroy the address space while its generated
// caller is still unwinding. Any subsequent decode would dereference it.
void testWasmJitTerminatedCpu() {
    auto& context = testContext();
    CPU* cpu = context.cpu;
    KMemory* memory = cpu->memory;
    bool terminating = context.thread->terminating;
    context.thread->terminating = true;
    cpu->memory = nullptr; // any accidental cache access must fail this test
    bool continued = cpu->runNextSingleOp();
    DecodedOp* next = cpu->getNextOp();
    cpu->memory = memory;
    context.thread->terminating = terminating;
    if (continued || next) testFail("terminated CPU decoded from a released address space");
}

// An interpreter-backed control transfer must leave generated code. Taken
// from upstream's IRET regression (4d3762913): EBX marks stale fallthrough.
void testWasmJitControlTransfer() {
    auto& context = testContext();
    CPU* cpu = context.cpu;
    testNewInstruction(0);
    const U32 sp = cpu->reg[4].u32;
    context.memory->writed(cpu->seg[SS].address + sp, 6);
    context.memory->writed(cpu->seg[SS].address + sp + 4, cpu->seg[CS].value);
    context.memory->writed(cpu->seg[SS].address + sp + 8, cpu->flags | 2);
    testPushCode8(0x31); testPushCode8(0xc0); // xor eax,eax
    testPushCode8(0x75); testPushCode8(0x01); // jnz +1 (not taken)
    testPushCode8(0xcf);                     // iret to offset 6
    testPushCode8(0x43);                     // stale inc ebx must not execute
    testRunCPU();
    DecodedOp* block = context.memory->getDecodedOp(TEST_CODE_ADDRESS);
    if (!block || block->pfn != cpu->thread->process->startJITOp)
        testFail("IRET regression must execute a generated block");
    if (cpu->reg[3].u32 != 0)
        testFail("WASM JIT executed stale fallthrough after IRET");
}
#endif

void testFail(const char* msg, ...) {
    TestContext& context = testContext();
    context.failed = true;

    char text[1024];
    va_list args;
    va_start(args, msg);
    vsnprintf(text, sizeof(text), msg, args);
    va_end(args);

    context.failures.push_back(text);
    if (!runningParallelTest) {
        failed("%s", text);
    }
}

#ifdef BOXEDWINE_HOST_EXCEPTIONS
void platformInitExceptionHandling();
#endif

void testRunParallel(const TestEntry* entries, size_t entryCount, U32 workerCount) {
    if (!entryCount) {
        return;
    }

    ensureParallelContexts(1);

    std::atomic<size_t> nextEntry(0);
    std::mutex printMutex;

    auto runEntry = [&](U32 contextIndex, size_t entryIndex) {
        bindParallelContext(contextIndex);
        TestContext& context = *currentContext;
        runningParallelTest = true;

        context.failed = false;
        context.failures.clear();
        // FD-41: a test must never take the whole suite down with it.
        // The guest fault machinery unwinds with bare int throws, and
        // kernel code under test can surface one on the harness thread
        // (or a test can throw a std::exception of its own); uncaught,
        // that called std::terminate and aborted the run mid-suite, so
        // no final count was ever printed. Catch everything here, record
        // the throw as THIS test's failure with its identity, and let
        // the remaining tests run: a throwing test still fails, visibly,
        // and the suite's final accounting stays trustworthy.
        try {
            entries[entryIndex].function();
        } catch (int e) {
            context.failed = true;
            context.failures.push_back("test threw an uncaught int exception out of its body (code " + std::to_string(e) + ")");
        } catch (const std::exception& e) {
            context.failed = true;
            context.failures.push_back(std::string("test threw an uncaught std::exception out of its body: ") + e.what());
        } catch (...) {
            context.failed = true;
            context.failures.push_back("test threw an uncaught non-standard exception out of its body");
        }
        {
            std::lock_guard<std::mutex> lock(printMutex);
            printf("%s", entries[entryIndex].name);
            printf(" ... ");
            if (context.failed) {
                printf("FAILED\n");
                for (const std::string& failure : context.failures) {
                    printf("  %s\n", failure.c_str());
                }
                failed("%s", entries[entryIndex].name);
            } else {
                printf("OK\n");
            }
            fflush(stdout);
        }
    };

    // The isolated run: every test in `indices` executes in a forked
    // child of the calling process (or in-process with
    // BW64_TEST_NO_FORK=1). Returns how many of the tests failed.
    // Verdict lines are printed as the tests run; the return value
    // carries the count because inside a parallel shard process
    // (below) failed() only updates that process's private counter,
    // so the parent recovers the count from here. The body is the
    // serial suite's own path, unchanged.
    auto runIsolated = [&](const std::vector<size_t>& indices) -> U32 {
        U32 failures = 0;
#ifdef BOXEDWINE_HOST_EXCEPTIONS
        // The JIT uses host faults to fall back for unaligned guest atomics.
        platformInitExceptionHandling();
#endif
#if defined(__linux__) && defined(BOXEDWINE_POSIX) && !defined(__EMSCRIPTEN__)
        // FD-41 (harness isolation): run each test in a forked child of
        // this process. Several tests deliberately leak guest processes
        // and threads whose host platform threads keep executing guest
        // code for the rest of the process lifetime; in one shared
        // process those leftovers fault, retire, and free state
        // underneath later tests, and the suite died mid-run in ways no
        // single test reproduces standalone (uncaught unwinds, host
        // faults in signal delivery on freed thread state, wedges in
        // the tail). Forking per test gives every test the standalone
        // environment it provably passes in -- same order, same code,
        // same assertions, the pre-created context inherited
        // copy-on-write -- while a child that crashes or throws can
        // only fail itself: the parent records the verdict (exit code,
        // or the killing signal by number) and the final count stays
        // trustworthy. BW64_TEST_NO_FORK=1 restores the in-process
        // path for bisection.
        if (!getenv("BW64_TEST_NO_FORK")) {
            fflush(stdout);
            for (size_t i : indices) {
                pid_t pid = fork();
                if (pid == 0) {
                    runEntry(0, i);
                    fflush(stdout);
                    fflush(stderr);
                    _exit(currentContext && currentContext->failed ? 1 : 0);
                }
                if (pid < 0) {
                    // No child to judge this test by: fail it loudly
                    // rather than skipping it.
                    std::lock_guard<std::mutex> lock(printMutex);
                    printf("%s ... FAILED\n  harness could not fork a child process for this test (%s)\n", entries[i].name, strerror(errno));
                    fflush(stdout);
                    failed("%s", entries[i].name); failures++;
                    continue;
                }
                int status = 0;
                pid_t waited;
                do {
                    waited = waitpid(pid, &status, 0);
                } while (waited < 0 && errno == EINTR);
                if (waited < 0) {
                    std::lock_guard<std::mutex> lock(printMutex);
                    printf("%s ... FAILED\n  harness lost track of this test's child process (%s)\n", entries[i].name, strerror(errno));
                    fflush(stdout);
                    failed("%s", entries[i].name); failures++;
                } else if (WIFEXITED(status)) {
                    // The child printed its own result line through
                    // runEntry; the parent only keeps the count.
                    if (WEXITSTATUS(status) != 0) {
                        failed("%s", entries[i].name); failures++;
                    }
                } else if (WIFSIGNALED(status)) {
                    std::lock_guard<std::mutex> lock(printMutex);
                    printf("%s ... FAILED\n  test process died with signal %d before it could report a result\n", entries[i].name, WTERMSIG(status));
                    fflush(stdout);
                    failed("%s", entries[i].name); failures++;
                } else {
                    std::lock_guard<std::mutex> lock(printMutex);
                    printf("%s ... FAILED\n  test process ended abnormally (wait status %d)\n", entries[i].name, status);
                    fflush(stdout);
                    failed("%s", entries[i].name); failures++;
                }
            }
            runningParallelTest = false;
            currentContext = nullptr;
            return failures;
        }
#endif
        for (size_t i : indices) {
            runEntry(0, i);
            if (currentContext && currentContext->failed) {
                failures++;
            }
        }
        runningParallelTest = false;
        currentContext = nullptr;
        return failures;
    };

    if (workerCount == 1) {
        std::vector<size_t> all(entryCount);
        for (size_t i = 0; i < entryCount; ++i) {
            all[i] = i;
        }
        runIsolated(all);
        return;
    }

#if defined(__linux__) && defined(BOXEDWINE_POSIX) && !defined(__EMSCRIPTEN__)
    if (!getenv("BW64_TEST_NO_SHARD")) {
        // FD-32: parallel workers used to be threads inside this one
        // process, and the multi-worker run died with SIGSEGV inside the
        // first handful of tests: the tests share the emulator's global
        // state, and FD-41 records why even sequential tests cannot
        // safely share a process, let alone concurrent ones. Parallelism
        // here is therefore process sharding instead: fork workerCount
        // shard processes, hand each a strided slice of the entries, and
        // let every shard run its slice through the exact isolated path
        // above -- each test still gets its own forked child of a
        // process holding only its own slice's history. A shard's
        // stdout/stderr go to a file the parent replays in shard order,
        // and its failure count returns over a pipe; a shard that dies
        // before reporting fails every test it owned, loudly, so the
        // final total can never silently shrink. BW64_TEST_NO_SHARD=1
        // restores the in-process thread path for bisection.
        U32 shardCount = (U32)std::min<size_t>(workerCount, entryCount);
        printf("running %zu tests in %u parallel shard processes\n", entryCount, shardCount);
        fflush(stdout);
        struct Shard {
            pid_t pid;
            int pipeFd;
            int outFd;
            char outPath[64];
            size_t assigned;
        };
        std::vector<Shard> shards;
        fflush(stdout);
        fflush(stderr);
        for (U32 s = 0; s < shardCount; ++s) {
            std::vector<size_t> indices;
            for (size_t i = s; i < entryCount; i += shardCount) {
                indices.push_back(i);
            }
            char pathTemplate[] = "/tmp/bw64test-shard-XXXXXX";
            int outFd = mkstemp(pathTemplate);
            int pipeFds[2] = {-1, -1};
            if (outFd < 0 || pipe(pipeFds) != 0) {
                // Could not set this shard up: run its slice here,
                // serially, rather than dropping the tests.
                if (outFd >= 0) {
                    close(outFd);
                    unlink(pathTemplate);
                }
                runIsolated(indices);
                continue;
            }
            pid_t pid = fork();
            if (pid == 0) {
                close(pipeFds[0]);
                dup2(outFd, STDOUT_FILENO);
                dup2(outFd, STDERR_FILENO);
                close(outFd);
                U32 failures = runIsolated(indices);
                const char* p = reinterpret_cast<const char*>(&failures);
                size_t left = sizeof(failures);
                while (left) {
                    ssize_t n = write(pipeFds[1], p, left);
                    if (n <= 0) {
                        break;
                    }
                    p += n;
                    left -= (size_t)n;
                }
                fflush(stdout);
                fflush(stderr);
                // _exit: inherited emulator state must not run atexit
                // handlers or static destructors a second time.
                _exit(0);
            }
            close(pipeFds[1]);
            if (pid < 0) {
                close(pipeFds[0]);
                close(outFd);
                unlink(pathTemplate);
                runIsolated(indices);
                continue;
            }
            Shard shard;
            shard.pid = pid;
            shard.pipeFd = pipeFds[0];
            shard.outFd = outFd;
            shard.assigned = indices.size();
            snprintf(shard.outPath, sizeof(shard.outPath), "%s", pathTemplate);
            shards.push_back(shard);
        }
        for (Shard& shard : shards) {
            U32 failures = 0;
            size_t got = 0;
            char* p = reinterpret_cast<char*>(&failures);
            while (got < sizeof(failures)) {
                ssize_t n = read(shard.pipeFd, p + got, sizeof(failures) - got);
                if (n < 0 && errno == EINTR) {
                    continue;
                }
                if (n <= 0) {
                    break;
                }
                got += (size_t)n;
            }
            close(shard.pipeFd);
            int status = 0;
            pid_t waited;
            do {
                waited = waitpid(shard.pid, &status, 0);
            } while (waited < 0 && errno == EINTR);
            if (got != sizeof(failures)) {
                printf("parallel shard process %d died before reporting (wait status %d): its %zu tests are counted as failed\n", (int)shard.pid, status, shard.assigned);
                fflush(stdout);
                failures = (U32)shard.assigned;
            }
            for (U32 k = 0; k < failures; ++k) {
                failed("shard result");
            }
            lseek(shard.outFd, 0, SEEK_SET);
            char buffer[8192];
            ssize_t n;
            while ((n = read(shard.outFd, buffer, sizeof(buffer))) > 0) {
                fwrite(buffer, 1, (size_t)n, stdout);
            }
            close(shard.outFd);
            unlink(shard.outPath);
        }
        fflush(stdout);
        return;
    }
#endif

    ensureParallelContexts(workerCount);
    std::vector<std::thread> workers;

    for (U32 i = 0; i < workerCount; ++i) {
        workers.push_back(std::thread([&, i]() {
#ifdef BOXEDWINE_HOST_EXCEPTIONS
            platformInitExceptionHandling();
#endif
            while (true) {
                size_t entryIndex = nextEntry.fetch_add(1);
                if (entryIndex >= entryCount) {
                    break;
                }
                runEntry(i, entryIndex);
            }
            runningParallelTest = false;
            currentContext = nullptr;
        }));
    }

    for (auto& worker : workers) {
        worker.join();
    }
}

#endif
