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

#include "soft_file_map.h"
#include "kmemory_soft.h"
#include "soft_ram.h"
#include "soft_mmu.h"

U8 FilePage::readb(MMU* mmu, U32 address) {
    onDemmand(mmu, address >> K_PAGE_SHIFT);
    return Page::getRWPage()->readb(mmu, address);
}

void FilePage::writeb(MMU* mmu, U32 address, U8 value) {
    onDemmand(mmu, address >> K_PAGE_SHIFT);
    Page::getRWPage()->writeb(mmu, address, value);
}

U16 FilePage::readw(MMU* mmu, U32 address) {
    onDemmand(mmu, address >> K_PAGE_SHIFT);
    return Page::getRWPage()->readw(mmu, address);
}

void FilePage::writew(MMU* mmu, U32 address, U16 value) {
    onDemmand(mmu, address >> K_PAGE_SHIFT);
    Page::getRWPage()->writew(mmu, address, value);
}

U32 FilePage::readd(MMU* mmu, U32 address) {
    onDemmand(mmu, address >> K_PAGE_SHIFT);
    return Page::getRWPage()->readd(mmu, address);
}

void FilePage::writed(MMU* mmu, U32 address, U32 value) {
    onDemmand(mmu, address >> K_PAGE_SHIFT);
    Page::getRWPage()->writed(mmu, address, value);
}

bool FilePage::canReadRam(MMU* mmu) {
    return false;
}

bool FilePage::canWriteRam(MMU* mmu) {
    return false;
}

U8* FilePage::getRamPtr(MMU* mmu, U32 page, bool write, bool force, U32 offset, U32 len) {
    if (!force) {
        return nullptr;
    }
    onDemmand(mmu, page);
    return Page::getRWPage()->getRamPtr(mmu, page, write, force, offset, len);
}

void FilePage::onDemmand(MMU* mmu, U32 pageIndex) {
    KThread* thread = KThread::currentThread();
    MappedFilePtr mappedFile;
    U64 fileOffset = 0;
    U32 fileOffsetPage = 0;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(thread->memory->mutex);
        if (mmu->getPageType() != PageType::File) {
            return;
        }
        mappedFile = thread->process->getMappedFile((U32)mmu->ramIndex);
        U32 address = pageIndex << K_PAGE_SHIFT;
        U32 mappedOffset = (address - mappedFile->address) & 0xfffff000;
        fileOffset = mappedOffset + mappedFile->offset;
        fileOffsetPage = (U32)(fileOffset >> K_PAGE_SHIFT);
        if (mappedFile->systemCacheEntry && fileOffsetPage < (U32)mappedFile->systemCacheEntry->data.size()) {
            RamPage cachedPage = mappedFile->systemCacheEntry->data[fileOffsetPage];
            if (cachedPage.value) {
                // System-cache hit: no file read is needed, so the page is
                // installed here without ever leaving the critical section.
                ramPageRetain(cachedPage);
                mmu->setPage(thread->memory, pageIndex, PageType::CopyOnWrite, cachedPage);
                ramPageRelease(cachedPage); // setPage retained it
                getMemData(thread->memory)->onPageChanged(pageIndex);
                return;
            }
        }
    }
    // The backing read happens WITHOUT the memory mutex. For a lazily
    // fetched archive (FsZipUrl) preadNative is a network fetch proxied
    // to the browser main thread; it can take seconds, and if the proxy
    // service stalls it may not return for a very long time. Holding
    // the process memory mutex across it (the previous shape) let one
    // stuck fetch freeze every thread of the process: a parked consumer
    // session was captured with all 69 guest-thread states byte-
    // identical over 90+ seconds, its remaining threads piled on this
    // mutex at their faulting instructions (FD-75). The fetch layer
    // itself already follows this discipline (its cache mutex is never
    // held across the fetch); the fault path now does the same. Two
    // threads may read the same page concurrently; the loser of the
    // install race below discards its copy.
    RamPage ramPage = ramPageAlloc();
    mappedFile->file->preadNative(ramPageGet(ramPage), fileOffset, K_PAGE_SIZE);
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(thread->memory->mutex);
    if (mmu->getPageType() != PageType::File) {
        // Another thread installed this page (or the mapping changed)
        // while the read was in flight; discard our copy.
        ramPageRelease(ramPage);
        return;
    }
    PageType pageType = PageType::Ram;
    if (mappedFile->systemCacheEntry && fileOffsetPage < (U32)mappedFile->systemCacheEntry->data.size()) {
        RamPage cachedPage = mappedFile->systemCacheEntry->data[fileOffsetPage];
        if (cachedPage.value) {
            // A concurrent fault populated the system cache first; use
            // the shared page so all mappings of it stay copy-on-write
            // consistent, and discard our private copy.
            ramPageRelease(ramPage);
            ramPage = cachedPage;
            ramPageRetain(ramPage);
            pageType = PageType::CopyOnWrite;
        } else {
            ramPageRetain(ramPage);
            ramPageMarkSystem(ramPage, true);
            mappedFile->systemCacheEntry->data[fileOffsetPage] = ramPage;
            pageType = PageType::CopyOnWrite;
        }
    }
    mmu->setPage(thread->memory, pageIndex, pageType, ramPage);
    ramPageRelease(ramPage); // setPageType retained ramPage
    getMemData(thread->memory)->onPageChanged(pageIndex);
}
