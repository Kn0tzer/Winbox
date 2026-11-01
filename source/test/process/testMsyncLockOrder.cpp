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
 *  the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef __TEST

#include "kerror.h"
#include "../cpu/testCPU.h"
#include "faultstage.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"

#include <stdlib.h>

#include <atomic>
#include <chrono>
#include <thread>

// FD-77: KProcess::msync must not hold the process mapped-files mutex
// across its writeback. The writeback reads the guest range, and
// reading a not-yet-resident page demand-faults it: the demand path
// takes the per-process memory mutex first and the mapped-files mutex
// second (FilePage::onDemmand holds the memory mutex while it calls
// KProcess::getMappedFile). An msync that holds the mapped-files mutex
// while its writeback faults therefore deadlocks against any thread
// that is demand-faulting a file page -- the exact configuration the
// FD-77 parked-consumer capture froze in: the memory-mutex owner sat
// inside FilePage::onDemmand waiting for the mapped-files mutex while
// the msync thread sat in the writeback waiting for the memory mutex,
// and every other thread of the process piled up behind them.
//
// The scenario is deterministic and self-unwedging. The test thread
// holds the memory mutex (playing the demand-fault holder). A worker
// thread msyncs a shared file mapping whose later pages were never
// touched: its writeback faults on the first untouched page and blocks
// on the memory mutex, which the worker's fault stage reports as
// FS_MEM_LOCK -- the sync point. At that moment a mapped-file lookup
// must still complete: it needs only the mapped-files mutex. If it
// cannot, msync is holding that mutex across the writeback; the test
// then releases the memory mutex (which unwinds the whole interleave)
// and fails. On fixed code the lookup completes while the demand is
// still blocked, and msync goes on to flush the dirtied page, which
// the test verifies in the host file.

namespace {

U64 hostMillies() {
    return (U64)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool waitForStage(KThread* thread, U32 stage, U32 timeoutMs) {
    const U64 deadline = hostMillies() + timeoutMs;
    while (hostMillies() < deadline) {
        if (thread->faultStage.load(std::memory_order_acquire) == stage) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return thread->faultStage.load(std::memory_order_acquire) == stage;
}

} // namespace

void testMsyncLockOrder() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KMemory* memory = context.memory;
    KThread* mainThread = context.thread;

    char dirTemplate[] = "/tmp/bw64msyncXXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for msync lock-order test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    const U32 mapLen = 3 * K_PAGE_SIZE;

    KFileDescriptorPtr created;
    U32 result = process->openFile(B("/"), B("/m.bin"), K_O_RDWR | K_O_CREAT, created);
    if ((S32)result < 0 || !created) {
        testFail("openFile(O_CREAT) for /m.bin failed: %d", (S32)result);
        return;
    }
    std::shared_ptr<KFile> file = std::dynamic_pointer_cast<KFile>(created->kobject);
    if (!file) {
        testFail("/m.bin did not produce a KFile");
        return;
    }
    U8 pageBuf[K_PAGE_SIZE];
    for (U32 p = 0; p < 3; p++) {
        memset(pageBuf, 0xA0 + (int)p, sizeof(pageBuf));
        if (file->pwriteNative(pageBuf, (S64)p * (S64)K_PAGE_SIZE, K_PAGE_SIZE) != K_PAGE_SIZE) {
            testFail("seeding /m.bin page %u failed", p);
            return;
        }
    }

    const U32 base = memory->mmap(mainThread, 0, mapLen, K_PROT_READ | K_PROT_WRITE,
                                  K_MAP_SHARED, (FD)created->handle, 0);
    if ((S32)base < 0) {
        testFail("mmap MAP_SHARED of /m.bin failed: %d", (S32)base);
        return;
    }
    // Reside page 0 and dirty one word; pages 1..2 stay non-resident
    // file pages, so the writeback faults inside the range.
    memory->writed(base + 0x40, 0x11223344);

    KThread* worker = process->createThread();
    std::atomic<bool> msyncDone{false};
    S32 msyncResult = -999;

    std::unique_lock<std::recursive_mutex> memHold(memory->mutex);
    std::thread msyncThread([&]() {
        KThread::setCurrentThread(worker);
        msyncResult = (S32)process->msync(worker, base, mapLen, 4); // MS_SYNC
        msyncDone.store(true, std::memory_order_release);
    });

    const bool formed = waitForStage(worker, FS_MEM_LOCK, 10000);
    bool lookupDone = false;
    if (formed) {
        std::atomic<bool> lookupFlag{false};
        std::thread lookupThread([&]() {
            MappedFilePtr found = process->getMappedFileForRange(base, K_PAGE_SIZE);
            if (found) {
                lookupFlag.store(true, std::memory_order_release);
            }
        });
        const U64 deadline = hostMillies() + 3000;
        while (hostMillies() < deadline && !lookupFlag.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        lookupDone = lookupFlag.load(std::memory_order_acquire);
        if (!lookupDone) {
            // Unwedge before failing: releasing the memory mutex lets
            // the writeback's demand complete, which ends the msync
            // and frees the mapped-files mutex for the lookup thread.
            memHold.unlock();
            msyncThread.join();
            lookupThread.join();
            testFail("msync lock order: getMappedFileForRange was still blocked after 3s "
                     "while msync's writeback demand was blocked on the memory mutex -- "
                     "msync holds mappedFilesMutex across guest-memory access, inverting "
                     "the demand path's memory -> mappedFiles order (FD-77 ABBA deadlock)");
            process->removeThread(worker);
            delete worker;
            return;
        }
        lookupThread.join();
    }
    if (memHold.owns_lock()) {
        memHold.unlock();
    }
    msyncThread.join();

    if (!formed) {
        testFail("msync lock order: scenario did not form -- the worker never blocked at "
                 "FS_MEM_LOCK inside the writeback (fault stage never observed)");
        process->removeThread(worker);
        delete worker;
        return;
    }
    if (!msyncDone.load(std::memory_order_acquire)) {
        testFail("msync lock order: msync thread did not complete");
    } else if (msyncResult != (S32)mapLen) {
        testFail("msync lock order: msync returned %d, expected %u (bytes written back)",
                 msyncResult, mapLen);
    }
    // The dirtied word must have reached the host file (the writeback
    // still does its job with the mutex released early).
    U8 check[4];
    const U8 expected[4] = {0x44, 0x33, 0x22, 0x11};
    if (file->preadNative(check, 0x40, 4) != 4 || memcmp(check, expected, 4) != 0) {
        testFail("msync lock order: dirtied word 0x11223344 not found in /m.bin after msync");
    }

    memory->unmap(base, mapLen);
    process->removeThread(worker);
    delete worker;
}

#endif
