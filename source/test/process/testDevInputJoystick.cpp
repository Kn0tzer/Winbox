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

#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../../io/fsopennode.h"
#include "devinput.h"
#include "kerror.h"
#include "../cpu/testCPU.h"
#ifdef BOXEDWINE_GUEST_X64
#include "kmemory64.h"
#endif

// The joystick node must present the evdev capability surface winebus's
// classification requires (EV_SYN|EV_KEY|EV_ABS, the gamepad button range,
// the six gamepad axes plus the hat), answer EVIOCGABS with Linux ranges,
// report live button state through EVIOCGKEY, and turn injected
// onGamepadButton/onGamepadAxis calls into input_event records. 64-bit
// readers must receive 24-byte records and working ioctls (ioctl64), not
// the 16-byte records / ENOTTY a 32-bit-only device would give them.

#define K3B_EVIOCGBIT(ev, len) (0x80000000u | ((U32)(len) << 16) | (0x4520u + (U32)(ev)))
#define K3B_EVIOCGABS(abs, len) (0x80000000u | ((U32)(len) << 16) | (0x4540u + (U32)(abs)))
#define K3B_EVIOCGNAME(len) (0x80000000u | ((U32)(len) << 16) | 0x4506u)
#define K3B_EVIOCGID(len) (0x80000000u | ((U32)(len) << 16) | 0x4502u)
#define K3B_EVIOCGKEY(len) (0x80000000u | ((U32)(len) << 16) | 0x4518u)

static bool bitIsSet(KMemory* memory, U32 address, U32 bit) {
    return (memory->readb(address + bit / 8) >> (bit % 8)) & 1;
}

static void joystickIoctlSetup(TestContext& context, U32 buffer) {
    // FsOpenNode::ioctl reads the argument pointer from EDX (IOCTL_ARG1).
    context.cpu->reg[2].u32 = buffer;
}

void testDevInputJoystick() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;
    constexpr U32 buf = TEST_HEAP_ADDRESS + 0x400;

    auto dummyNode = std::make_shared<FsFileNode>(998, 0, B("/dev/input/event5"), B(""), B(""), false, false, nullptr);
    FsOpenNode* device = openDevInputJoystick(dummyNode, K_O_RDWR, 0);
    if (!device) {
        testFail("openDevInputJoystick returned null");
        return;
    }

    // EVIOCGBIT(0): the device type mask must advertise SYN/KEY/ABS.
    memory->memset(buf, 0, 32);
    joystickIoctlSetup(context, buf);
    U32 result = device->ioctl(context.thread, K3B_EVIOCGBIT(0, 4));
    if ((S32)result < 0 || memory->readd(buf) != ((1u << K_EV_SYN) | (1u << K_EV_KEY) | (1u << K_EV_ABS))) {
        testFail("joystick EVIOCGBIT(0) mask wrong: result=%d mask=0x%x", (S32)result, memory->readd(buf));
    }

    // EVIOCGBIT(EV_KEY): the full gamepad button range, and no mouse buttons.
    memory->memset(buf, 0, 64);
    joystickIoctlSetup(context, buf);
    result = device->ioctl(context.thread, K3B_EVIOCGBIT(K_EV_KEY, 64));
    if ((S32)result < 0) {
        testFail("joystick EVIOCGBIT(EV_KEY) failed: %d", (S32)result);
    } else {
        for (U32 code = 0x130; code <= 0x13e; code++) {
            if (!bitIsSet(memory, buf, code)) {
                testFail("joystick EVIOCGBIT(EV_KEY) missing gamepad button 0x%x", code);
            }
        }
        if (bitIsSet(memory, buf, K_BTN_LEFT) || bitIsSet(memory, buf, K_BTN_RIGHT)) {
            testFail("joystick EVIOCGBIT(EV_KEY) must not claim mouse buttons");
        }
    }

    // EVIOCGBIT(EV_ABS): X/Y/Z/RX/RY/RZ plus HAT0X/HAT0Y.
    memory->memset(buf, 0, 64);
    joystickIoctlSetup(context, buf);
    result = device->ioctl(context.thread, K3B_EVIOCGBIT(K_EV_ABS, 64));
    if ((S32)result < 0) {
        testFail("joystick EVIOCGBIT(EV_ABS) failed: %d", (S32)result);
    } else {
        const U32 axes[] = {K_ABS_X, K_ABS_Y, K_ABS_Z, K_ABS_RX, K_ABS_RY, K_ABS_RZ, K_ABS_HAT0X, K_ABS_HAT0Y};
        for (U32 axis : axes) {
            if (!bitIsSet(memory, buf, axis)) {
                testFail("joystick EVIOCGBIT(EV_ABS) missing axis 0x%x", axis);
            }
        }
    }

    // EVIOCGABS ranges: sticks signed resting at 0, triggers 0..255, hat -1..1.
    struct AbsExpect { U32 axis; S32 value; S32 min; S32 max; };
    const AbsExpect absExpect[] = {
        {K_ABS_X, 0, -32768, 32767}, {K_ABS_Y, 0, -32768, 32767},
        {K_ABS_RX, 0, -32768, 32767}, {K_ABS_RY, 0, -32768, 32767},
        {K_ABS_Z, 0, 0, 255}, {K_ABS_RZ, 0, 0, 255},
        {K_ABS_HAT0X, 0, -1, 1}, {K_ABS_HAT0Y, 0, -1, 1},
    };
    for (const AbsExpect& expect : absExpect) {
        memory->memset(buf, 0xcd, 24);
        joystickIoctlSetup(context, buf);
        result = device->ioctl(context.thread, K3B_EVIOCGABS(expect.axis, 24));
        if ((S32)result < 0 || (S32)memory->readd(buf) != expect.value ||
            (S32)memory->readd(buf + 4) != expect.min || (S32)memory->readd(buf + 8) != expect.max) {
            testFail("joystick EVIOCGABS(0x%x) wrong: result=%d value=%d min=%d max=%d",
                     expect.axis, (S32)result, (S32)memory->readd(buf),
                     (S32)memory->readd(buf + 4), (S32)memory->readd(buf + 8));
        }
    }

    // Identity: one stable name and id.
    memory->memset(buf, 0, 64);
    joystickIoctlSetup(context, buf);
    result = device->ioctl(context.thread, K3B_EVIOCGNAME(64));
    if ((S32)result <= 0 || !memory->readString(buf).contains("gamepad")) {
        testFail("joystick EVIOCGNAME did not return the gamepad name");
    }
    memory->memset(buf, 0, 8);
    joystickIoctlSetup(context, buf);
    result = device->ioctl(context.thread, K3B_EVIOCGID(8));
    if ((S32)result < 0 || memory->readw(buf) != 3) { // BUS_USB
        testFail("joystick EVIOCGID wrong bustype: %d", memory->readw(buf));
    }

    // With no input the device is poll-not-ready and reads would block.
    if (device->isReadReady()) {
        testFail("joystick reported read-ready with an empty event queue");
    }
    U8 record[64];
    result = device->readNative(record, sizeof(record));
    if ((S32)result != -K_EWOULDBLOCK) {
        testFail("joystick empty read returned %d, expected -EWOULDBLOCK", (S32)result);
    }

    // Injected button press: EVIOCGKEY reflects it, and the read path emits
    // an EV_KEY record followed by SYN_REPORT (16-byte records for the
    // 32-bit test process).
    onGamepadButton(K_BTN_SOUTH, true);
    if (!device->isReadReady()) {
        testFail("joystick not read-ready after an injected button press");
    }
    memory->memset(buf, 0, 64);
    joystickIoctlSetup(context, buf);
    result = device->ioctl(context.thread, K3B_EVIOCGKEY(64));
    if ((S32)result < 0 || !bitIsSet(memory, buf, K_BTN_SOUTH)) {
        testFail("joystick EVIOCGKEY did not reflect the pressed BTN_SOUTH");
    }
    result = device->readNative(record, sizeof(record));
    if (result != 32) {
        testFail("joystick 32-bit read returned %d bytes, expected 2 16-byte records", result);
    } else {
        U32 type0 = *(U16*)(record + 8) | ((U32)(*(U16*)(record + 10)) << 16);
        U32 value0 = *(U32*)(record + 12);
        U32 type1 = *(U16*)(record + 24) | ((U32)(*(U16*)(record + 26)) << 16);
        if (type0 != (K_EV_KEY | ((U32)K_BTN_SOUTH << 16)) || value0 != 1 ||
            type1 != (K_EV_SYN | ((U32)K_SYN_REPORT << 16))) {
            testFail("joystick records wrong: [type/code=0x%x value=%d] [type/code=0x%x]",
                     type0, value0, type1);
        }
    }
    onGamepadButton(K_BTN_SOUTH, false);
    memory->memset(buf, 0, 64);
    joystickIoctlSetup(context, buf);
    device->ioctl(context.thread, K3B_EVIOCGKEY(64));
    if (bitIsSet(memory, buf, K_BTN_SOUTH)) {
        testFail("joystick EVIOCGKEY still showed BTN_SOUTH after release");
    }
    result = device->readNative(record, sizeof(record)); // drain release + SYN
    if (result != 32) {
        testFail("joystick release read returned %d bytes, expected 32", result);
    }

    // Injected axis motion updates EVIOCGABS state and emits EV_ABS records.
    onGamepadAxis(K_ABS_X, -12345);
    onGamepadAxis(K_ABS_Z, 200);
    memory->memset(buf, 0, 24);
    joystickIoctlSetup(context, buf);
    device->ioctl(context.thread, K3B_EVIOCGABS(K_ABS_X, 24));
    if ((S32)memory->readd(buf) != -12345) {
        testFail("joystick EVIOCGABS(ABS_X) did not track injected motion: %d", (S32)memory->readd(buf));
    }
    memory->memset(buf, 0, 24);
    joystickIoctlSetup(context, buf);
    device->ioctl(context.thread, K3B_EVIOCGABS(K_ABS_Z, 24));
    if ((S32)memory->readd(buf) != 200) {
        testFail("joystick EVIOCGABS(ABS_Z) did not track injected trigger: %d", (S32)memory->readd(buf));
    }
    result = device->readNative(record, sizeof(record));
    if (result != 48) { // ABS_X, ABS_Z, SYN
        testFail("joystick axis read returned %d bytes, expected 48", result);
    }
    onGamepadAxis(K_ABS_X, 0);
    onGamepadAxis(K_ABS_Z, 0);
    device->readNative(record, sizeof(record)); // drain

#ifdef BOXEDWINE_GUEST_X64
    // 64-bit guests: ioctls go through ioctl64/KMemory64, and records are
    // 24-byte input_event structs.
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory64 = context.process->memory64;
    constexpr U64 page = 0x77000000ULL;
    if (!memory64->getPageFlags(page >> K64_PAGE_SHIFT)) {
        memory64->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3);
    }
    memory64->memsetGuest(page, 0, 64);
    result = device->ioctl64(K3B_EVIOCGBIT(K_EV_KEY, 64), page, memory64);
    if ((S32)result < 0) {
        testFail("joystick ioctl64 EVIOCGBIT(EV_KEY) failed: %d", (S32)result);
    } else {
        bool south = (memory64->readb(page + K_BTN_SOUTH / 8) >> (K_BTN_SOUTH % 8)) & 1;
        if (!south) {
            testFail("joystick ioctl64 EVIOCGBIT(EV_KEY) missing BTN_SOUTH");
        }
    }
    memory64->memsetGuest(page, 0, 24);
    result = device->ioctl64(K3B_EVIOCGABS(K_ABS_RY, 24), page, memory64);
    if ((S32)result < 0 || (S32)memory64->readd(page + 4) != -32768 ||
        (S32)memory64->readd(page + 8) != 32767) {
        testFail("joystick ioctl64 EVIOCGABS(ABS_RY) wrong range");
    }

    bool was64 = context.process->is64Bit;
    KThread* previousThread = KThread::currentThread();
    KThread::setCurrentThread(context.thread);
    context.process->is64Bit = true;
    onGamepadButton(K_BTN_START, true);
    result = device->readNative(record, sizeof(record));
    context.process->is64Bit = was64;
    KThread::setCurrentThread(previousThread);
    if (result != 48) { // two 24-byte records
        testFail("joystick 64-bit read returned %d bytes, expected 2 24-byte records", result);
    } else {
        U16 type = *(U16*)(record + 16);
        U16 code = *(U16*)(record + 18);
        S32 value = *(S32*)(record + 20);
        U16 type1 = *(U16*)(record + 40);
        if (type != K_EV_KEY || code != K_BTN_START || value != 1 || type1 != K_EV_SYN) {
            testFail("joystick 64-bit records wrong: type=%d code=0x%x value=%d next=%d",
                     type, code, value, type1);
        }
    }
    onGamepadButton(K_BTN_START, false);
    device->readNative(record, sizeof(record)); // drain
#endif

    device->close();
    delete device;
}

#endif
