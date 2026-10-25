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
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef __TEST

#include "../../io/fsfilenode.h"
#include "../../io/fsopennode.h"
#include "devdsp.h"
#include "kerror.h"
#include "../cpu/testCPU.h"
#ifdef BOXEDWINE_GUEST_X64
#include "kmemory64.h"
#endif

// /dev/dsp capture contract: reading an open-for-read device must never
// return 0 (a fake EOF) — it returns captured bytes, -EWOULDBLOCK when
// capture is live but empty, or -ENOTSUP when the audio backend cannot
// capture at all. GETISPACE must follow the same rule in both ABIs: real
// numbers when capture works, a loud error when it does not — never the
// old success-with-all-zeros answer, and never -ENODEV on the 32-bit path.

#define K3B_SNDCTL_DSP_GETISPACE 0x500d
#define K3B_SNDCTL_DSP_GETOSPACE 0x500c

void testDevDspCapture() {
    TestContext& context = testContext();
    auto node = std::make_shared<FsFileNode>(999, 0, B("/dev/dsp"), B(""), B(""), false, false, nullptr);
    FsOpenNode* dsp = openDevDsp(node, K_O_RDWR, 0);
    if (!dsp) {
        testFail("openDevDsp returned null");
        return;
    }

    U8 buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    U32 readResult = dsp->readNative(buffer, sizeof(buffer));
    if ((S32)readResult == 0) {
        testFail("dsp capture read returned 0 (fake EOF) instead of data, -EWOULDBLOCK or -ENOTSUP");
    }
    bool captureLive = (S32)readResult > 0 || (S32)readResult == -K_EWOULDBLOCK;
    if (!captureLive && (S32)readResult != -K_ENOTSUP) {
        testFail("dsp capture read returned %d, expected data, -EWOULDBLOCK or -ENOTSUP", (S32)readResult);
    }

    // 32-bit GETISPACE: never -ENODEV; success only when capture is live.
    constexpr U32 infoAt = TEST_HEAP_ADDRESS + 0xb00;
    context.thread->memory->memset(infoAt, 0xcd, 16);
    context.cpu->reg[2].u32 = infoAt; // EDX = ioctl argument
    U32 result = dsp->ioctl(context.thread, K3B_SNDCTL_DSP_GETISPACE);
    if ((S32)result == -K_ENODEV) {
        testFail("32-bit dsp GETISPACE returned -ENODEV (device exists; wrong errno)");
    }
    if (captureLive && (S32)result != 0) {
        testFail("32-bit dsp GETISPACE failed (%d) while capture is live", (S32)result);
    }
    if (!captureLive && (S32)result != -K_ENOTSUP) {
        testFail("32-bit dsp GETISPACE returned %d without capture, expected -ENOTSUP", (S32)result);
    }
    if ((S32)result == 0) {
        S32 fragments = (S32)context.thread->memory->readd(infoAt);
        S32 fragsize = (S32)context.thread->memory->readd(infoAt + 8);
        if (fragments <= 0 || fragsize <= 0) {
            testFail("dsp GETISPACE reported success with empty fragment info (%d/%d)", fragments, fragsize);
        }
    }
    // Playback introspection is unaffected.
    context.thread->memory->memset(infoAt, 0, 16);
    result = dsp->ioctl(context.thread, K3B_SNDCTL_DSP_GETOSPACE);
    if ((S32)result != 0) {
        testFail("32-bit dsp GETOSPACE broke: %d", (S32)result);
    }

#ifdef BOXEDWINE_GUEST_X64
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory64 = context.process->memory64;
    constexpr U64 page = 0x77000000ULL;
    if (!memory64->getPageFlags(page >> K64_PAGE_SHIFT)) {
        memory64->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3);
    }
    memory64->memsetGuest(page, 0xcd, 16);
    result = dsp->ioctl64(K3B_SNDCTL_DSP_GETISPACE, page, memory64);
    if (captureLive && (S32)result != 0) {
        testFail("64-bit dsp GETISPACE failed (%d) while capture is live", (S32)result);
    }
    if (!captureLive && (S32)result != -K_ENOTSUP) {
        testFail("64-bit dsp GETISPACE returned %d without capture (the old empty-success lie), expected -ENOTSUP",
                 (S32)result);
    }
    if ((S32)result == 0) {
        S32 fragments = (S32)memory64->readd(page);
        S32 fragsize = (S32)memory64->readd(page + 8);
        if (fragments <= 0 || fragsize <= 0) {
            testFail("64-bit dsp GETISPACE reported success with empty fragment info (%d/%d)", fragments, fragsize);
        }
    }
#endif

    dsp->close();
    delete dsp;
}

#endif
