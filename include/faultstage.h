/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
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

#ifndef __FAULTSTAGE_H__
#define __FAULTSTAGE_H__

#include "kthread.h"
#include "kmemory.h"
#include "ksystem.h"

// FD-77: per-thread fault-stage markers for the demand/fault path.
//
// A parked session's frozen threads are host-asleep in a wait somewhere
// in the page-demand path, executing no guest ops (proven by the dump's
// noprogress_ms). The path has three locks and one external call, and a
// thread can wait at any of them without any existing instrument seeing
// it: the per-process KMemory::mutex, the RAM-page allocator's mutex,
// a mapped file's position mutex (KFile::filePosMutex, taken inside
// KFile::preadNative), and the backing read itself (for a lazily fetched
// archive member a browser-proxied fetch; for another backing file
// whatever its readNative does). The stages below are set on entry to
// each of those places so the guest thread dump (bw64_dump_guest_threads)
// can print, for every running thread, exactly which wait it is in and
// for how long. All state is per-thread atomics on KThread, written only
// by the thread itself; the dump reads it locklessly, preserving the
// dump's never-block contract.
//
// Stage discipline: FaultStageScope saves and restores the previous
// stage (and its entry time and page), so stages nest -- a demand fault
// entered from inside another demand stage returns to the outer stage
// when it completes. A stage's age is measured from when the stage was
// ENTERED, not from the last nested excursion.

enum BwFaultStage : U32 {
    FS_NONE = 0,         // not in the demand path
    FS_DEMAND = 1,       // in an onDemmand implementation, before any lock
    FS_MEM_LOCK = 2,     // acquiring KMemory::mutex (the tracked lock)
    FS_MEM_WORK = 3,     // holding KMemory::mutex, doing the section's work
    FS_RAM_ALLOC = 4,    // in (or acquiring the mutex of) ramPageAlloc
    FS_FILEPOS_LOCK = 5, // acquiring a mapped file's position mutex
    FS_BACKING_READ = 6, // inside the backing read (readNative)
    FS_INSTALL = 7,      // installing the faulted page under KMemory::mutex
};

inline const char* bwFaultStageName(U32 stage) {
    switch (stage) {
        case FS_NONE: return "none";
        case FS_DEMAND: return "demand";
        case FS_MEM_LOCK: return "mem-lock";
        case FS_MEM_WORK: return "mem-work";
        case FS_RAM_ALLOC: return "ram-alloc";
        case FS_FILEPOS_LOCK: return "filepos-lock";
        case FS_BACKING_READ: return "backing-read";
        case FS_INSTALL: return "install";
        default: return "?";
    }
}

// RAII stage marker. Pass page = 0xFFFFFFFF to keep the thread's current
// faultPage (used by stages that do not know the faulting page, e.g. the
// KFile layer). A null thread (host threads with no guest thread) makes
// the scope a no-op.
class FaultStageScope {
public:
    FaultStageScope(KThread* thread, U32 stage, U32 page = 0xFFFFFFFFu) : thread(thread) {
        if (!thread) return;
        savedStage = thread->faultStage.load(std::memory_order_relaxed);
        savedSinceMs = thread->faultStageSinceMs.load(std::memory_order_relaxed);
        savedPage = thread->faultPage.load(std::memory_order_relaxed);
        thread->setFaultStage(stage, page == 0xFFFFFFFFu ? savedPage : page);
    }
    ~FaultStageScope() {
        if (!thread) return;
        thread->faultStage.store(savedStage, std::memory_order_release);
        thread->faultStageSinceMs.store(savedSinceMs, std::memory_order_relaxed);
        thread->faultPage.store(savedPage, std::memory_order_relaxed);
    }
    FaultStageScope(const FaultStageScope&) = delete;
    FaultStageScope& operator=(const FaultStageScope&) = delete;

private:
    KThread* thread;
    U32 savedStage = 0;
    U64 savedSinceMs = 0;
    U32 savedPage = 0;
};

// The demand path's acquisition of KMemory::mutex, with the ownership
// record the dump prints (KMemory::mutexOwnerTid and friends). Behaves
// exactly like the BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX unique_lock it
// replaces at demand-path sites: same recursive mutex, same scope-bound
// hold. While blocked it leaves the acquiring thread at FS_MEM_LOCK;
// once acquired the thread is at FS_MEM_WORK (a caller may then set a
// more specific stage, e.g. FS_INSTALL); on scope exit the thread's
// previous stage is restored.
//
// Ownership semantics: the claim is recorded after the real mutex is
// acquired and released before it is unlocked, so while a demand-path
// thread holds the mutex the record names it. A recursion through this
// lock increments the depth; a recursion through the plain macro is
// invisible to the record (documented on the fields in kmemory.h).
class MemoryDemandLock {
public:
    explicit MemoryDemandLock(KMemory* memory) : mem(memory), thread(KThread::currentThread()) {
        if (thread) {
            savedStage = thread->faultStage.load(std::memory_order_relaxed);
            savedSinceMs = thread->faultStageSinceMs.load(std::memory_order_relaxed);
            thread->setFaultStage(FS_MEM_LOCK, thread->faultPage.load(std::memory_order_relaxed));
        }
        BOXEDWINE_MUTEX_LOCK(mem->mutex);
        claim();
        if (thread) {
            thread->setFaultStage(FS_MEM_WORK, thread->faultPage.load(std::memory_order_relaxed));
        }
    }
    ~MemoryDemandLock() {
        releaseClaim();
        BOXEDWINE_MUTEX_UNLOCK(mem->mutex);
        if (thread) {
            thread->faultStage.store(savedStage, std::memory_order_release);
            thread->faultStageSinceMs.store(savedSinceMs, std::memory_order_relaxed);
        }
    }
    MemoryDemandLock(const MemoryDemandLock&) = delete;
    MemoryDemandLock& operator=(const MemoryDemandLock&) = delete;

private:
    void claim() {
        const U32 tid = thread ? thread->id : 0;
        if (!tid) return;
        claimed = true;
        if (mem->mutexOwnerTid.load(std::memory_order_relaxed) == tid) {
            mem->mutexOwnerDepth.fetch_add(1, std::memory_order_relaxed);
        } else {
            // We hold the real mutex, so any previous record is a stale
            // claim from a hold that has already ended; take it over.
            mem->mutexOwnerTid.store(tid, std::memory_order_relaxed);
            mem->mutexOwnerDepth.store(1, std::memory_order_relaxed);
            mem->mutexOwnerSinceMs.store((U64)(KSystem::getMicroCounter() / 1000), std::memory_order_relaxed);
        }
    }
    void releaseClaim() {
        if (!claimed) return;
        claimed = false;
        const U32 tid = thread ? thread->id : 0;
        if (mem->mutexOwnerTid.load(std::memory_order_relaxed) == tid &&
            mem->mutexOwnerDepth.fetch_sub(1, std::memory_order_relaxed) == 1) {
            mem->mutexOwnerTid.store(0, std::memory_order_relaxed);
        }
    }

    KMemory* mem;
    KThread* thread;
    U32 savedStage = 0;
    U64 savedSinceMs = 0;
    bool claimed = false;
};

#endif
