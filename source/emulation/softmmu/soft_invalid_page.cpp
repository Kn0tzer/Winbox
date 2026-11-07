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

#include "soft_invalid_page.h"
#include "kmemory_soft.h"
#include "soft_mmu.h"

// seg_mapper returns normally only when its on-demand mapping
// succeeded (the 32-bit Wine syscall_frame window); a delivered
// guest handler or a fatal fault throws instead. The mapping
// replaces this page, so the access must be re-dispatched to the
// page that is installed now -- previously the read answered a
// fabricated 0 (right only by the fresh page being zero-filled)
// and the write was silently dropped (frontier review D5 audit).
U8 InvalidPage::readb(MMU* mmu, U32 address) {
    KThread* thread = KThread::currentThread();
    thread->seg_mapper(address, true, false);
    Page* page = mmu->getPage();
    if (page != this) {
        return page->readb(mmu, address);
    }
    return 0;
}

void InvalidPage::writeb(MMU* mmu, U32 address, U8 value) {
    KThread* thread = KThread::currentThread();
    thread->seg_mapper(address, false, true);
    Page* page = mmu->getPage();
    if (page != this) {
        page->writeb(mmu, address, value);
    } else {
        klog_fmt("BW64-FAIL invalid-page-write op=writeb addr=0x%x outcome=dropped reason=page-unchanged-after-seg_mapper-return", address);
    }
}

U16 InvalidPage::readw(MMU* mmu, U32 address) {
    KThread* thread = KThread::currentThread();
    thread->seg_mapper(address, true, false);
    Page* page = mmu->getPage();
    if (page != this) {
        return page->readw(mmu, address);
    }
    return 0;
}

void InvalidPage::writew(MMU* mmu, U32 address, U16 value) {
    KThread* thread = KThread::currentThread();
    thread->seg_mapper(address, false, true);
    Page* page = mmu->getPage();
    if (page != this) {
        page->writew(mmu, address, value);
    } else {
        klog_fmt("BW64-FAIL invalid-page-write op=writew addr=0x%x outcome=dropped reason=page-unchanged-after-seg_mapper-return", address);
    }
}

U32 InvalidPage::readd(MMU* mmu, U32 address) {
    KThread* thread = KThread::currentThread();
    thread->seg_mapper(address, true, false);
    Page* page = mmu->getPage();
    if (page != this) {
        return page->readd(mmu, address);
    }
    return 0;
}

void InvalidPage::writed(MMU* mmu, U32 address, U32 value) {
    KThread* thread = KThread::currentThread();
    thread->seg_mapper(address, false, true);
    Page* page = mmu->getPage();
    if (page != this) {
        page->writed(mmu, address, value);
    } else {
        klog_fmt("BW64-FAIL invalid-page-write op=writed addr=0x%x outcome=dropped reason=page-unchanged-after-seg_mapper-return", address);
    }
}

bool InvalidPage::canReadRam(MMU* mmu) {
    return false;
}

bool InvalidPage::canWriteRam(MMU* mmu) {
    return false;
}

U8* InvalidPage::getRamPtr(MMU* mmu, U32 page, bool write, bool force, U32 offset, U32 len) {
    return nullptr;
}

void InvalidPage::onDemmand(MMU* mmu, U32 page) {
}
