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
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"
#include "soft_ram.h"

#define NATIVE_PAGE_REF_COUNT 0

static BOXEDWINE_MUTEX ramMutex;

// FD-77: ownership record for ramMutex, in the same spirit as the
// KMemory::mutex record (see faultstage.h): the allocator's sections
// are short and none of them blocks on anything but the platform
// reserve/commit calls in ramPageAlloc, so a dump that finds this
// owner unchanged across dumps has localized a freeze to the
// allocator -- and a thread waiting at FS_RAM_ALLOC with owner 0 here
// is waiting behind a section the record does not cover. Written only
// while holding ramMutex (RamMutexOwnerScope is declared after the
// lock at each site), read locklessly by the guest thread dump.
static std::atomic<U32> ramMutexOwnerTid{0};
static std::atomic<U64> ramMutexOwnerSinceMs{0};

namespace {
struct RamMutexOwnerScope {
    RamMutexOwnerScope() {
        KThread* t = KThread::currentThread();
        prevTid = ramMutexOwnerTid.exchange(t ? t->id : 0, std::memory_order_relaxed);
        prevSinceMs = ramMutexOwnerSinceMs.exchange((U64)(KSystem::getMicroCounter() / 1000), std::memory_order_relaxed);
    }
    ~RamMutexOwnerScope() {
        ramMutexOwnerTid.store(prevTid, std::memory_order_relaxed);
        ramMutexOwnerSinceMs.store(prevSinceMs, std::memory_order_relaxed);
    }
    U32 prevTid = 0;
    U64 prevSinceMs = 0;
};
} // namespace

int bw64RamMutexStats(U32* ownerTid, U64* ownerAgeMs) {
    const U32 tid = ramMutexOwnerTid.load(std::memory_order_relaxed);
    *ownerTid = tid;
    if (!tid) {
        *ownerAgeMs = 0;
        return 0;
    }
    const U64 since = ramMutexOwnerSinceMs.load(std::memory_order_relaxed);
    const U64 now = (U64)(KSystem::getMicroCounter() / 1000);
    *ownerAgeMs = (since && since <= now) ? now - since : 0;
    return 1;
}
int allocatedRamPages;

class RamInfo {
public:
    U16 refCount : 14;
    U16 isNative : 1;
    U16 isSystem : 1;
};

static std::unordered_map<U64, RamInfo> refCounts;
static std::vector<RAM_TYPE> freeIndexes;

struct alignas(K_PAGE_SIZE) AlignedU8 {
    U8 data;
};

std::vector<U8*> allocatedPages4k;
std::vector<U8*> allocatedPages4kChunk;
std::vector<AlignedU8*> allocatedPages;
#define CHUNK_SIZE_4K 8

RamPage ramPageAlloc() {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(ramMutex);
    RamMutexOwnerScope ramOwnerScope;
    if (freeIndexes.size()) {
        RamPage found;
        found.value = freeIndexes.back();
        freeIndexes.pop_back();
        refCounts[found.value].refCount = 1;
        memset((U8*)(found.value << K_PAGE_SHIFT), 0, K_PAGE_SIZE);
        allocatedRamPages++;
        return found;
    }    
    if (KSystem::canJitUse4KPage) {
        U8* result = (U8*)Platform::reserveNativeMemory64k(CHUNK_SIZE_4K);
        allocatedPages4kChunk.push_back(result);
        allocatedPages4k.push_back(result);

        U8* pages = result;
        U32 count = (CHUNK_SIZE_4K * 16 / 2) - 1; // -1 so that there is an uncommitted page at the end
        for (U32 i = 0; i < count; i++) {
            pages += K_PAGE_SIZE; // keep uncommitted page between each committed so that if a read/write crosses a page boundry it will generate an exception
            Platform::commitNativeMemoryPage(pages);
            if (i == 0) {
                result = pages;
            } else {
                RAM_TYPE index = ((RAM_TYPE)pages) >> K_PAGE_SHIFT;
                freeIndexes.push_back(index);
                allocatedRamPages++;
            }
            pages += K_PAGE_SIZE;
        }
        RAM_TYPE index = ((RAM_TYPE)result) >> K_PAGE_SHIFT;

        memset(result, 0, K_PAGE_SIZE);
        RamPage found;
        found.value = index;

        refCounts[index].refCount = 1;
        allocatedRamPages++;
        return found;
    } else {
        AlignedU8* result = new AlignedU8[64]; // need to create a few since an aligned new will over allocate to make the alignment work.
        allocatedPages.push_back(result);
        RAM_TYPE index = ((RAM_TYPE)result) >> K_PAGE_SHIFT;
        for (int i = 1; i < 64; i++) {
            allocatedRamPages++;
            freeIndexes.push_back(index + i);
        }

        memset(result, 0, K_PAGE_SIZE);
        RamPage found;
        found.value = index;

        refCounts[index].refCount = 1;
        allocatedRamPages++;
        return found;
    }
}

RamPage ramPageAllocNative(U8* native) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(ramMutex);
    RamMutexOwnerScope ramOwnerScope;
    RAM_TYPE index = ((RAM_TYPE)native) >> K_PAGE_SHIFT;

    if (((U64)native) & 0xfff) {
        kpanic("ramPageAllocNative must be aligned to a page");
    }
    refCounts[index].refCount = 1;
    refCounts[index].isNative = 1;
    RamPage result;
    result.value = index;
    return result;
}

void shutdownRam() {
    refCounts.clear();
    if (KSystem::canJitUse4KPage) {
        for (U8* p : allocatedPages4kChunk) {
            Platform::releaseNativeMemory(p, CHUNK_SIZE_4K * 64 * 1024);
        }
        allocatedPages4kChunk.clear();
        allocatedPages4k.clear();
    } else {        
        for (AlignedU8* p : allocatedPages) {
            delete[] p;
        }
        allocatedPages.clear();
    }
    freeIndexes.clear();
    allocatedRamPages = 0;
}

RamPage ramPageAllocNativeContinuous(U8* native, U32 pageCount) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(ramMutex);
    RamMutexOwnerScope ramOwnerScope;
    RAM_TYPE ramIndex = ((RAM_TYPE)native) >> K_PAGE_SHIFT;

    for (U32 i = 0; i < pageCount; i++) {
        refCounts[ramIndex + i].refCount = 1;
        refCounts[ramIndex + i].isNative = 1;
        native += K_PAGE_SIZE;
    }
    RamPage result;
    result.value = ramIndex;
    return result;
}

void ramPageRetain(RamPage page) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(ramMutex);
    RamMutexOwnerScope ramOwnerScope;
    refCounts[page.value].refCount++;
}

U32 ramPageUseCount(RamPage page) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(ramMutex);
    RamMutexOwnerScope ramOwnerScope;
    return refCounts[page.value].refCount;
}

void ramPageMarkSystem(RamPage page, bool isSystem) {
    refCounts[page.value].isSystem = isSystem ? 1 : 0;
}

bool ramPageIsSystem(RamPage page) {
    return refCounts[page.value].isSystem != 0;
}

bool ramPageIsNative(RamPage page) {
    return refCounts[page.value].isNative != 0;
}

void ramPageRelease(RamPage page) {
    if (page.value == 0) {
        return;
    }
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(ramMutex);
    RamMutexOwnerScope ramOwnerScope;
    refCounts[page.value].refCount--;
    if (refCounts[page.value].refCount == 0) {               
        if (refCounts[page.value].isNative) {
            refCounts[page.value].isNative = 0;
            refCounts[page.value].isSystem = 0;
        } else {
            allocatedRamPages--;
            freeIndexes.push_back(page.value);
        }
    }    
}