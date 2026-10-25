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

#include "devinput.h"
#include "ksignal.h"
#include "kscheduler.h"
#include "knativesystem.h"

#include <string.h>

#include "../../io/fsvirtualopennode.h"
#ifdef BOXEDWINE_GUEST_X64
#include "kmemory64.h"
#endif

class EventData {
public:
    U64 time;
    U16 type;
    U16 code;
    S32 value;
};

#define INPUT_PROP_POINTER		    0x00	/* needs a pointer */
#define INPUT_PROP_DIRECT		    0x01	/* direct input devices */
#define INPUT_PROP_BUTTONPAD		0x02	/* has button(s) under pad */
#define INPUT_PROP_SEMI_MT		    0x03	/* touch rectangle only */
#define INPUT_PROP_TOPBUTTONPAD		0x04	/* softbuttons at top of pad */
#define INPUT_PROP_POINTING_STICK	0x05	/* is a pointing stick */
#define INPUT_PROP_ACCELEROMETER	0x06	/* has accelerometer */

#define EV_SYN                  0x00
#define EV_KEY                  0x01
#define EV_REL                  0x02
#define EV_ABS                  0x03
#define EV_MSC                  0x04
#define EV_SW                   0x05
#define EV_LED                  0x11
#define EV_SND                  0x12
#define EV_REP                  0x14
#define EV_FF                   0x15
#define EV_PWR                  0x16
#define EV_FF_STATUS            0x17
#define EV_MAX                  0x1f

class DevInput : public FsVirtualOpenNode {
public:
    DevInput(const std::shared_ptr<FsNode>& node, U32 flags) : 
        FsVirtualOpenNode(node, flags), 
        asyncProcessId(0),
        asyncProcessFd(0), 
        bustype(0),
        vendor(0),
        product(0),
        version(0),
        mask(0),
        prop(0),
        bufferCond(std::make_shared<BoxedWineCondition>(B("DevInput::bufferCond"))),
        clearOnExit(nullptr) {}

    virtual ~DevInput() {
        if (clearOnExit) {
            *clearOnExit = nullptr;
        }
    }

    // From FsOpenNode
    U32 ioctl(KThread* thread, U32 request) override;
#ifdef BOXEDWINE_GUEST_X64
    U32 ioctl64(U32 request, U64 argAddr, KMemory64* mem) override;
#endif
    // The evdev ioctl cases are identical for both guest widths; only the
    // memory accessor and the argument's address width differ.
    template <typename M, typename A>
    U32 ioctlForMemory(M* memory, U32 request, A buffer);
    U32 readNative(U8* buffer, U32 len) override;
    U32 writeNative(U8* buffer, U32 len) override;
    void waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) override;
    void setAsync(bool isAsync) override;
    bool isAsync() override;
    bool isWriteReady() override;
    bool isReadReady() override;

    U32 asyncProcessId;
    U32 asyncProcessFd;
    U16 bustype;
    U16 vendor;
    U16 product;
    U16 version;
    BString name;
    U32 mask;
    U32 prop;
    BOXEDWINE_CONDITION bufferCond;
    std::queue<EventData> eventQueue;
    DevInput** clearOnExit;
};

class DevInputTouch : public DevInput {
public:
    DevInputTouch(const std::shared_ptr<FsNode>& node, U32 flags);

    // from FsOpenNode
    U32 ioctl(KThread* thread, U32 request) override;

    U32 lastX;
    U32 lastY;
};

class DevInputMouse : public DevInput {
public:
    DevInputMouse(const std::shared_ptr<FsNode>& node, U32 flags);

    // from FsOpenNode
    U32 ioctl(KThread* thread, U32 request) override;
};

class DevInputKeyboard : public DevInput {
public:
    DevInputKeyboard(const std::shared_ptr<FsNode>& node, U32 flags);

    // FsOpenNode
    U32 ioctl(KThread* thread, U32 request) override;
};

static DevInputTouch* touchEvents;

FsOpenNode* openDevInputTouch(const std::shared_ptr<FsNode>& node, U32 flags, U32 data) {
    touchEvents = new DevInputTouch(node, flags);
    touchEvents->clearOnExit = (DevInput**)&touchEvents;
    return touchEvents;
}

static DevInputMouse* mouseEvents;

FsOpenNode* openDevInputMouse(const std::shared_ptr<FsNode>& node, U32 flags, U32 data) {
    mouseEvents = new DevInputMouse(node, flags);
    mouseEvents->clearOnExit = (DevInput**)&mouseEvents;
    return mouseEvents;
}

static DevInputKeyboard* keyboardEvents;

FsOpenNode* openDevInputKeyboard(const std::shared_ptr<FsNode>& node, U32 flags, U32 data) {
    keyboardEvents = new DevInputKeyboard(node, flags);
    keyboardEvents->clearOnExit = (DevInput**)&keyboardEvents;
    return keyboardEvents;
}

// :TODO: can this be blocking
U32 DevInput::readNative(U8* buffer, U32 len) {
    // struct input_event is 24 bytes for a 64-bit guest (two 64-bit time
    // fields) and 16 bytes for a 32-bit guest. The read path runs on the
    // reader's thread, so the reader's process width decides the record
    // size; emitting 16-byte records to a 64-bit reader misaligns every
    // record after the first.
    KThread* reader = KThread::currentThread();
    bool guest64 = reader && reader->process && reader->process->is64Bit;
    U32 result = 0;
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->bufferCond);
    if (guest64) {
        while (this->eventQueue.size() && result+24<=len) {
            const EventData& e = this->eventQueue.front();
            U64* t = (U64*)(buffer+result);
            t[0] = e.time / 1000000; // seconds
            t[1] = e.time % 1000000; // microseconds
            U16* f = (U16*)(buffer+result+16);
            f[0] = e.type;
            f[1] = e.code;
            *(S32*)(buffer+result+20) = e.value;
            result+=24;
            this->eventQueue.pop();
        }
    } else {
        while (this->eventQueue.size() && result+16<=len) {
            const EventData& e = this->eventQueue.front();
            U32* b = (U32*)(buffer+result);
            b[0] = (U32) (e.time / 1000000); // seconds
            b[1] = (U32) (e.time % 1000000); // microseconds
            b[2] = e.type | ((U32)e.code) << 16;
            b[3] = e.value;
            result+=16;
            this->eventQueue.pop();
        }
    }
    if (result == 0) {
        return -K_EWOULDBLOCK;
    }
    return result;
}

U32 DevInput::writeNative(U8* buffer, U32 len) {
    return len;
}

void DevInput::waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) {
    if (events & K_POLLIN) {
        BOXEDWINE_CONDITION_ADD_PARENT(this->bufferCond, parentCondition);
    } else {
        BOXEDWINE_CONDITION_REMOVE_PARENT(this->bufferCond, parentCondition);
    }
}

template <typename M, typename A>
static void writeBit(M* memory, A address, U32 bit) {
    U32 b = bit/8;
    U32 p = bit % 8;
    U32 value = memory->readb(address+b);
    value|=(1<<p);
    memory->writeb(address+b, value);
}

static void inputMemset(KMemory* memory, U32 address, U32 len) {
    memory->memset(address, 0, len);
}
#ifdef BOXEDWINE_GUEST_X64
static void inputMemset(KMemory64* memory, U64 address, U32 len) {
    memory->memsetGuest(address, 0, len);
}
#endif
static void inputWrite(KMemory* memory, U32 address, const void* data, U32 len) {
    memory->memcpy(address, (const U8*)data, len);
}
#ifdef BOXEDWINE_GUEST_X64
static void inputWrite(KMemory64* memory, U64 address, const void* data, U32 len) {
    memory->memcpyToGuest(address, data, len);
}
#endif

//    struct input_absinfo {
//        __s32 value;
//        __s32 minimum;
//        __s32 maximum;
//        __s32 fuzz;
//        __s32 flat;
//        __s32 resolution;
//    };
template <typename M, typename A>
static void writeAbs(M* memory, A address, U32 value, U32 min, U32 max, U32 resolution = 96) {
    memory->writed(address, value);
    memory->writed(address+4, min);
    memory->writed(address+8, max);
    memory->writed(address+12, 0);
    memory->writed(address+16, 0);
    memory->writed(address+20, resolution);
}

template <typename M, typename A>
U32 DevInput::ioctlForMemory(M* memory, U32 request, A buffer) {
    U32 cmd = request & 0xFFFF;

    switch (cmd) {
        case 0x4501: { // EVIOCGVERSION
            U32 len = (request & 0x1fff0000) >> 16;
            if (len<4)
                kpanic_fmt("Bad length for EVIOCGVERSION: %d", len);
            memory->writed(buffer, this->version);
            return 4;
        }
        case 0x4502: { // EVIOCGID
//            struct input_id {
//                __u16 bustype;
//                __u16 vendor;
//                __u16 product;
//                __u16 version;
//                };
            U32 len = (request & 0x1fff0000) >> 16;
            if (len!=8)
                kpanic_fmt("Bad length for EVIOCGID: %d",len);
            memory->writew(buffer, this->bustype);
            memory->writew(buffer + 2, this->vendor);
            memory->writew(buffer + 4, this->product);
            memory->writew(buffer + 6, this->version);
            return 0;
        }
        case 0x4506: { // EVIOCGNAME
            U32 len = (request & 0x1fff0000) >> 16;
            U32 todo = std::min((U32)name.length(), len);
            inputWrite(memory, buffer, this->name.c_str(), todo);
            return todo;
        }
       case 0x4507: { // EVIOCGPHYS
            U32 len = (request & 0x1fff0000) >> 16;
            U32 todo = std::min((U32)name.length(), len);
            inputWrite(memory, buffer, this->name.c_str(), todo);
            return todo;
        }
        case 0x4508: { //  EVIOCGUNIQ
            U32 len = (request & 0x1fff0000) >> 16;
            U32 todo = std::min((U32)name.length(), len);
            inputWrite(memory, buffer, this->name.c_str(), todo);
            return todo;
        }
        case 0x4509: { // EVIOCGPROP
            U32 len = (request & 0x1fff0000) >> 16;
            if (len<4)
                kpanic_fmt("Bad length for EVIOCGBIT: %d", len);
            memory->writed(buffer, this->prop);
            return 4;
        }
        case 0x4518: { // EVIOCGKEY
            U32 len = (request & 0x1fff0000) >> 16;
            inputMemset(memory, buffer, len);
            return 0;
        }
        case 0x4519: { // EVIOCGLED
            U32 len = (request & 0x1fff0000) >> 16;
            inputMemset(memory, buffer, len);
            return 0;
        }
        case 0x451b: { // EVIOCGSW
            U32 len = (request & 0x1fff0000) >> 16;
            inputMemset(memory, buffer, len);
            return 0;
        }
        case 0x4520: { // EVIOCGBIT
            U32 len = (request & 0x1fff0000) >> 16;
            if (len<4)
                kpanic_fmt("Bad length for EVIOCGBIT: %d", len);
            memory->writed(buffer, this->mask);
            return 4;
        }
        case 0x4524: { // EVIOCGBIT(EV_MSC)
            U32 len = (request & 0x1fff0000) >> 16;
            inputMemset(memory, buffer, len);
            return 0;
        }
        case 0x4525: { // EVIOCGBIT(EV_SW) - Switches
            U32 len = (request & 0x1fff0000) >> 16;
            inputMemset(memory, buffer, len);
            return 0;
        }
        case 0x4532: { // EVIOCGBIT(EV_SND) - Sound Effects
            U32 len = (request & 0x1fff0000) >> 16;
            inputMemset(memory, buffer, len);
            return 0;
        }
        case 0x4535: { // EVIOCGBIT(EV_FF) - Force Feedback
            U32 len = (request & 0x1fff0000) >> 16;
            inputMemset(memory, buffer, len);
            return 0;
        }
        case 0x540B: // TCFLSH
            return 0;		
    }
    return -1;
}

U32 DevInput::ioctl(KThread* thread, U32 request) {
    CPU* cpu = thread->cpu;
    return this->ioctlForMemory(thread->memory, request, (U32)IOCTL_ARG1);
}

#ifdef BOXEDWINE_GUEST_X64
U32 DevInput::ioctl64(U32 request, U64 argAddr, KMemory64* mem) {
    return this->ioctlForMemory(mem, request, argAddr);
}
#endif

void DevInput::setAsync(bool isAsync) {
    KProcessPtr process = KThread::currentThread()->process;
    if (isAsync) {
        if (this->asyncProcessId && this->asyncProcessId!=process->id) {
            kpanic_fmt("touch_setAsync only supports one process: %d tried to attached but %d already has it", process->id, this->asyncProcessId);
        } else {
            this->asyncProcessId = process->id;
            // :TODO: pass in fildes
            this->asyncProcessFd = KThread::currentThread()->cpu->reg[3].u32;
        }
    } else {
        if (process->id == this->asyncProcessId) {
            this->asyncProcessId = 0;
            this->asyncProcessFd = 0;
        }
    }
}

bool DevInput::isAsync() {
    return this->asyncProcessId == KThread::currentThread()->process->id;
}

bool DevInput::isWriteReady() {
    return this->eventQueue.size()<1000;
}

bool DevInput::isReadReady() {
    return this->eventQueue.size()>0;
}


DevInputTouch::DevInputTouch(const std::shared_ptr<FsNode>& node, U32 flags) : DevInput(node, flags), lastX(0), lastY(0) {
    this->bustype = 3;
    this->vendor = 0;
    this->product = 0;
    this->version = 1;
    this->prop = (1 << INPUT_PROP_DIRECT) | (1<<INPUT_PROP_POINTER);
    this->name = B("BoxedWine touchpad");
    this->mask = (1<<K_EV_SYN)|(1<<K_EV_KEY)|(1<<K_EV_ABS);
}

U32 DevInputTouch::ioctl(KThread* thread, U32 request) {
    CPU* cpu = KThread::currentThread()->cpu;
    KMemory* memory = thread->memory;

    switch (request & 0xFFFF) {
        case 0x4521: { // EVIOCGBIT, EV_KEY
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            U32 result = K_BTN_MIDDLE;
            result = (result+7)/8;
            memory->memset(buffer, 0, len);
            writeBit(memory, buffer, K_BTN_LEFT);
            writeBit(memory, buffer, K_BTN_MIDDLE);
            writeBit(memory, buffer, K_BTN_RIGHT);
            return result;
        }
        case 0x4522: { // EVIOCGBIT, EV_REL
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 1;
        }
        case 0x4523: { // EVIOCGBIT, EV_ABS
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            memory->writeb(buffer, (1 << K_ABS_X) | (1 << K_ABS_Y));
            return 1;
        }
        case 0x4531: { // EVIOCGBIT, EV_LED
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 1;
        }
        case 0x4540: { // EVIOCGABS (ABS_X)
            U32 len = (request & 0x1fff0000) >> 16;
            U32 address = IOCTL_ARG1;
            if (len<24)
                kpanic("Bad length for EVIOCGABS (ABS_X)");
            writeAbs(memory, address, this->lastX, 0, KNativeSystem::getScreen()->screenWidth());
            return 0;
        }
        case 0x4541: { // EVIOCGABS (ABS_Y)
            int len = (request & 0x1fff0000) >> 16;
            int address = IOCTL_ARG1;
            if (len<24)
                kpanic("Bad length for EVIOCGABS (ABS_X)");
            writeAbs(memory, address, this->lastY, 0, KNativeSystem::getScreen()->screenHeight());
            return 0;
        }
        default:
            return DevInput::ioctl(thread, request);
    }
    return -1;
}

DevInputMouse::DevInputMouse(const std::shared_ptr<FsNode>& node, U32 flags) : DevInput(node, flags) {
    this->bustype = 3;
    this->vendor = 0x046d;
    this->product = 0xc52b;
    this->version = 0x0111;
    this->name = B("BoxedWine mouse");
    this->mask = (1<<K_EV_SYN)|(1<<K_EV_KEY)|(1<<K_EV_REL);
}

U32 DevInputMouse::ioctl(KThread* thread, U32 request) {
    CPU* cpu = thread->cpu;
    KMemory* memory = thread->memory;

    switch (request & 0xFFFF) {
        case 0x4521: { // EVIOCGBIT, EV_KEY
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            U32 result = K_BTN_MIDDLE;
            result = (result+7)/8;
            memory->memset(buffer, 0, len);
            writeBit(memory, buffer, K_BTN_LEFT);
            writeBit(memory, buffer, K_BTN_MIDDLE);
            writeBit(memory, buffer, K_BTN_RIGHT);
            writeBit(memory, buffer, K_BTN_MOUSEWHEEL_UP);
            writeBit(memory, buffer, K_BTN_MOUSEWHEEL_DOWN);
            return result;
        }
        case 0x4522: { // EVIOCGBIT, EV_REL
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            memory->writeb(buffer, (1 << K_REL_X) | (1 << K_REL_Y));
            return 1;
        }
        case 0x4523: { // EVIOCGBIT, EV_ABS
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 1;
        }
        case 0x4531: { // EVIOCGBIT, EV_LED
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 1;
        }
        case 0x4540: { // EVIOCGABS (ABS_X)
            U32 len = (request & 0x1fff0000) >> 16;
            U32 address = IOCTL_ARG1;
            if (len<24)
                kpanic("Bad length for EVIOCGABS (ABS_X)");
            writeAbs(memory, address, 0, 0, 0);
            return 0;
        }
        case 0x4541: { // EVIOCGABS (ABS_Y)
            int len = (request & 0x1fff0000) >> 16;
            int address = IOCTL_ARG1;
            if (len<24)
                kpanic("Bad length for EVIOCGABS (ABS_X)");
            writeAbs(memory, address, 0, 0, 0);
            return 0;
        }
        default:
            return DevInput::ioctl(thread, request);
    }
    return -1;
}


DevInputKeyboard::DevInputKeyboard(const std::shared_ptr<FsNode>& node, U32 flags) : DevInput(node, flags) {
    this->bustype = 0x11;
    this->vendor = 1;
    this->product = 1;
    this->version = 0xab41;
    this->name = B("BoxedWine Keyboard");
    this->mask = (1<<K_EV_SYN)|(1<<K_EV_KEY);
}

U32 DevInputKeyboard::ioctl(KThread* thread, U32 request) {
    CPU* cpu = thread->cpu;
    KMemory* memory = thread->memory;

    switch (request & 0xFFFF) {
        case 0x4521: { // EVIOCGBIT, EV_KEY
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            writeBit(memory, buffer, K_KEY_ESC);
            writeBit(memory, buffer, K_KEY_1);
            writeBit(memory, buffer, K_KEY_2);
            writeBit(memory, buffer, K_KEY_3);
            writeBit(memory, buffer, K_KEY_4);
            writeBit(memory, buffer, K_KEY_5);
            writeBit(memory, buffer, K_KEY_6);
            writeBit(memory, buffer, K_KEY_7);
            writeBit(memory, buffer, K_KEY_8);
            writeBit(memory, buffer, K_KEY_9);
            writeBit(memory, buffer, K_KEY_0);
            writeBit(memory, buffer, K_KEY_MINUS);
            writeBit(memory, buffer, K_KEY_EQUAL);
            writeBit(memory, buffer, K_KEY_BACKSPACE);
            writeBit(memory, buffer, K_KEY_TAB);
            writeBit(memory, buffer, K_KEY_Q);
            writeBit(memory, buffer, K_KEY_W);
            writeBit(memory, buffer, K_KEY_E);
            writeBit(memory, buffer, K_KEY_R);
            writeBit(memory, buffer, K_KEY_T);
            writeBit(memory, buffer, K_KEY_Y);
            writeBit(memory, buffer, K_KEY_U);
            writeBit(memory, buffer, K_KEY_I);
            writeBit(memory, buffer, K_KEY_O);
            writeBit(memory, buffer, K_KEY_P);
            writeBit(memory, buffer, K_KEY_LEFTBRACE);
            writeBit(memory, buffer, K_KEY_RIGHTBRACE);
            writeBit(memory, buffer, K_KEY_ENTER);
            writeBit(memory, buffer, K_KEY_LEFTCTRL);
            writeBit(memory, buffer, K_KEY_A);
            writeBit(memory, buffer, K_KEY_S);
            writeBit(memory, buffer, K_KEY_D);
            writeBit(memory, buffer, K_KEY_F);
            writeBit(memory, buffer, K_KEY_G);
            writeBit(memory, buffer, K_KEY_H);
            writeBit(memory, buffer, K_KEY_J);
            writeBit(memory, buffer, K_KEY_K);
            writeBit(memory, buffer, K_KEY_L);
            writeBit(memory, buffer, K_KEY_SEMICOLON);
            writeBit(memory, buffer, K_KEY_APOSTROPHE);
            writeBit(memory, buffer, K_KEY_GRAVE);
            writeBit(memory, buffer, K_KEY_LEFTSHIFT);
            writeBit(memory, buffer, K_KEY_BACKSLASH);
            writeBit(memory, buffer, K_KEY_Z);
            writeBit(memory, buffer, K_KEY_X);
            writeBit(memory, buffer, K_KEY_C);
            writeBit(memory, buffer, K_KEY_V);
            writeBit(memory, buffer, K_KEY_B);
            writeBit(memory, buffer, K_KEY_N);
            writeBit(memory, buffer, K_KEY_M);
            writeBit(memory, buffer, K_KEY_COMMA);
            writeBit(memory, buffer, K_KEY_DOT);
            writeBit(memory, buffer, K_KEY_SLASH);
            writeBit(memory, buffer, K_KEY_RIGHTSHIFT);
            writeBit(memory, buffer, K_KEY_LEFTALT);
            writeBit(memory, buffer, K_KEY_SPACE);
            writeBit(memory, buffer, K_KEY_CAPSLOCK);
            writeBit(memory, buffer, K_KEY_F1);
            writeBit(memory, buffer, K_KEY_F2);
            writeBit(memory, buffer, K_KEY_F3);
            writeBit(memory, buffer, K_KEY_F4);
            writeBit(memory, buffer, K_KEY_F5);
            writeBit(memory, buffer, K_KEY_F6);
            writeBit(memory, buffer, K_KEY_F7);
            writeBit(memory, buffer, K_KEY_F8);
            writeBit(memory, buffer, K_KEY_F9);
            writeBit(memory, buffer, K_KEY_F10);
            writeBit(memory, buffer, K_KEY_NUMLOCK);
            writeBit(memory, buffer, K_KEY_SCROLLLOCK);

            writeBit(memory, buffer, K_KEY_F11);
            writeBit(memory, buffer, K_KEY_F12);
            writeBit(memory, buffer, K_KEY_RIGHTCTRL);
            writeBit(memory, buffer, K_KEY_RIGHTALT);
            writeBit(memory, buffer, K_KEY_HOME);
            writeBit(memory, buffer, K_KEY_UP);
            writeBit(memory, buffer, K_KEY_PAGEUP);
            writeBit(memory, buffer, K_KEY_LEFT);
            writeBit(memory, buffer, K_KEY_RIGHT);
            writeBit(memory, buffer, K_KEY_END);
            writeBit(memory, buffer, K_KEY_DOWN);
            writeBit(memory, buffer, K_KEY_PAGEDOWN);
            writeBit(memory, buffer, K_KEY_INSERT);
            writeBit(memory, buffer, K_KEY_DELETE);
            writeBit(memory, buffer, K_KEY_PAUSE);
            return (K_KEY_PAUSE+7)/8;
        }
        case 0x4522: { // EVIOCGBIT, EV_REL
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            memory->writeb(buffer, 0);
            return 1;
        }
        case 0x4523: { // EVIOCGBIT, EV_ABS
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            memory->writeb(buffer, 0);
            return 1;
        }
        case 0x4531: { // EVIOCGBIT, EV_LED
            int len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 1;
        }
        default:
            return DevInput::ioctl(thread, request);
    }
    return -1;
}


// The gamepad axis set winebus's evdev classification looks for: two
// sticks, two triggers, one hat. Ranges follow the Linux gamepad
// convention (signed sticks resting at 0, byte triggers, ternary hat).
static const struct { U32 code; S32 min; S32 max; } kJoystickAxes[] = {
    {K_ABS_X, -32768, 32767}, {K_ABS_Y, -32768, 32767}, {K_ABS_Z, 0, 255},
    {K_ABS_RX, -32768, 32767}, {K_ABS_RY, -32768, 32767}, {K_ABS_RZ, 0, 255},
    {K_ABS_HAT0X, -1, 1}, {K_ABS_HAT0Y, -1, 1},
};

static int joystickAxisIndex(U32 code) {
    for (U32 i = 0; i < sizeof(kJoystickAxes) / sizeof(kJoystickAxes[0]); i++) {
        if (kJoystickAxes[i].code == code) {
            return (int)i;
        }
    }
    return -1;
}

class DevInputJoystick : public DevInput {
public:
    DevInputJoystick(const std::shared_ptr<FsNode>& node, U32 flags);

    // from FsOpenNode
    U32 ioctl(KThread* thread, U32 request) override {
        CPU* cpu = thread->cpu;
        return this->joystickIoctl(thread->memory, request, (U32)IOCTL_ARG1);
    }
#ifdef BOXEDWINE_GUEST_X64
    U32 ioctl64(U32 request, U64 argAddr, KMemory64* mem) override {
        return this->joystickIoctl(mem, request, argAddr);
    }
#endif

    template <typename M, typename A>
    U32 joystickIoctl(M* memory, U32 request, A buffer);

    S32 axisValues[8];
    bool buttonStates[15]; // K_BTN_SOUTH..K_BTN_THUMBR
};

DevInputJoystick::DevInputJoystick(const std::shared_ptr<FsNode>& node, U32 flags) : DevInput(node, flags) {
    this->bustype = 3; // BUS_USB
    this->vendor = 0x045e;
    this->product = 0x028e;
    this->version = 0x0110;
    this->name = B("BoxedWine gamepad");
    this->mask = (1<<K_EV_SYN)|(1<<K_EV_KEY)|(1<<K_EV_ABS);
    memset(this->axisValues, 0, sizeof(this->axisValues));
    memset(this->buttonStates, 0, sizeof(this->buttonStates));
}

template <typename M, typename A>
U32 DevInputJoystick::joystickIoctl(M* memory, U32 request, A buffer) {
    U32 cmd = request & 0xFFFF;
    U32 len = (request & 0x1fff0000) >> 16;
    switch (cmd) {
        case 0x4521: { // EVIOCGBIT(EV_KEY)
            inputMemset(memory, buffer, len);
            for (U32 code = K_BTN_SOUTH; code <= K_BTN_THUMBR; code++) {
                if (code / 8 < len) {
                    writeBit(memory, buffer, code);
                }
            }
            return (K_BTN_THUMBR + 7) / 8;
        }
        case 0x4523: { // EVIOCGBIT(EV_ABS)
            inputMemset(memory, buffer, len);
            for (U32 i = 0; i < sizeof(kJoystickAxes) / sizeof(kJoystickAxes[0]); i++) {
                if (kJoystickAxes[i].code / 8 < len) {
                    writeBit(memory, buffer, kJoystickAxes[i].code);
                }
            }
            return (K_ABS_HAT0Y + 7) / 8;
        }
        case 0x4518: { // EVIOCGKEY: the live button state, not the base
                       // class's zero-filled answer
            inputMemset(memory, buffer, len);
            for (U32 code = K_BTN_SOUTH; code <= K_BTN_THUMBR; code++) {
                if (this->buttonStates[code - K_BTN_SOUTH] && code / 8 < len) {
                    writeBit(memory, buffer, code);
                }
            }
            return 0;
        }
        default: {
            if (cmd >= 0x4540 && cmd < 0x4560) { // EVIOCGABS(abs)
                int index = joystickAxisIndex(cmd - 0x4540);
                if (index >= 0) {
                    if (len < 24) {
                        kpanic("Bad length for EVIOCGABS (joystick)");
                    }
                    writeAbs(memory, buffer, (U32)this->axisValues[index],
                             (U32)kJoystickAxes[index].min, (U32)kJoystickAxes[index].max, 0);
                    return 0;
                }
            }
            return DevInput::ioctlForMemory(memory, request, buffer);
        }
    }
}

void queueEvent(DevInput* queue, U32 type, U32 code, U32 value, U64 time) {
    EventData data;

    data.time = time;
    data.type = type;
    data.code = code;
    data.value = value;
    if (queue) {        
        queue->eventQueue.push(data);
    }
}

/*
void onMouseMove(U32 x, U32 y) {
    U32 send = 0;

    if (x!=lastX) {
        queueEvent(&mouseEvents, K_EV_REL, K_REL_X, x-lastX);
        lastX = x;
        send = 1;
    }
    if (y!=lastY) {
        queueEvent(&mouseEvents, K_EV_REL, K_REL_Y, y-lastY);
        lastY = y;
        send = 1;
    }
    if (send) {
        queueEvent(&mouseEvents, K_EV_SYN, K_SYN_REPORT, 0);
        if (mouseEvents.asyncProcessId) {
            struct KProcess* process = getProcessById(mouseEvents.asyncProcessId);
            if (process)
                signalProcess(process, K_SIGIO);
        }
    }
}

void onMouseButtonUp(U32 button) {
    if (button == 0)
        queueEvent(&mouseEvents, K_EV_KEY, K_BTN_LEFT, 0);
    else if (button == 2)
        queueEvent(&mouseEvents, K_EV_KEY, K_BTN_MIDDLE, 0);
    else if (button == 1)
        queueEvent(&mouseEvents, K_EV_KEY, K_BTN_RIGHT, 0);
    else
        return;
    queueEvent(&mouseEvents, K_EV_SYN, K_SYN_REPORT, 0);
    if (mouseEvents.asyncProcessId) {
        struct KProcess* process = getProcessById(mouseEvents.asyncProcessId);
        if (process)
            signalProcess(process, K_SIGIO);
    }
}

void onMouseButtonDown(U32 button) {
    if (button == 0)
        queueEvent(&mouseEvents, K_EV_KEY, K_BTN_LEFT, 1);
    else if (button == 2)
        queueEvent(&mouseEvents, K_EV_KEY, K_BTN_MIDDLE, 1);
    else if (button == 1)
        queueEvent(&mouseEvents, K_EV_KEY, K_BTN_RIGHT, 1);
    else
        return;
    queueEvent(&mouseEvents, K_EV_SYN, K_SYN_REPORT, 0);
    if (mouseEvents.asyncProcessId) {
        struct KProcess* process = getProcessById(mouseEvents.asyncProcessId);
        if (process)
            signalProcess(process, K_SIGIO);
    }
}
*/

void postSendEvent(DevInput* events, U64 time) {
    if (!events)
        return;
    queueEvent(events, K_EV_SYN, K_SYN_REPORT, 0, time);
    if (events->asyncProcessId) {
        KProcessPtr process = KSystem::getProcess(events->asyncProcessId);
        if (process) {
            process->signalIO(K_POLL_IN, 0, events->asyncProcessFd);		
        }
    }        
}

static DevInputJoystick* joystickEvents;

FsOpenNode* openDevInputJoystick(const std::shared_ptr<FsNode>& node, U32 flags, U32 data) {
    joystickEvents = new DevInputJoystick(node, flags);
    joystickEvents->clearOnExit = (DevInput**)&joystickEvents;
    return joystickEvents;
}

void onGamepadButton(U32 code, bool down) {
    if (!joystickEvents) {
        return;
    }
    if (code < K_BTN_SOUTH || code > K_BTN_THUMBR) {
        return;
    }
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(joystickEvents->bufferCond);
    joystickEvents->buttonStates[code - K_BTN_SOUTH] = down;
    queueEvent(joystickEvents, K_EV_KEY, code, down ? 1 : 0, time);
    postSendEvent(joystickEvents, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(joystickEvents->bufferCond);
}

void onGamepadAxis(U32 axis, S32 value) {
    if (!joystickEvents) {
        return;
    }
    int index = joystickAxisIndex(axis);
    if (index < 0) {
        return;
    }
    if (value < kJoystickAxes[index].min) {
        value = kJoystickAxes[index].min;
    }
    if (value > kJoystickAxes[index].max) {
        value = kJoystickAxes[index].max;
    }
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(joystickEvents->bufferCond);
    if (joystickEvents->axisValues[index] == value) {
        return;
    }
    joystickEvents->axisValues[index] = value;
    queueEvent(joystickEvents, K_EV_ABS, axis, value, time);
    postSendEvent(joystickEvents, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(joystickEvents->bufferCond);
}

void onMouseButtonUp(U32 button) {
    if (!touchEvents) {
        return;
    }
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(touchEvents->bufferCond);

    if (button == 0)
        queueEvent(touchEvents, K_EV_KEY, K_BTN_LEFT, 0, time);
    else if (button == 2)
        queueEvent(touchEvents, K_EV_KEY, K_BTN_MIDDLE, 0, time);
    else if (button == 1)
        queueEvent(touchEvents, K_EV_KEY, K_BTN_RIGHT, 0, time);
    else
        return;
    postSendEvent(touchEvents, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(touchEvents->bufferCond);
}

void onMouseButtonDown(U32 button) {
    if (!touchEvents) {
        return;
    }
    U64 time = KSystem::getSystemTimeAsMicroSeconds();

    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(touchEvents->bufferCond);
    if (button == 0)
        queueEvent(touchEvents, K_EV_KEY, K_BTN_LEFT, 1, time);
    else if (button == 2)
        queueEvent(touchEvents, K_EV_KEY, K_BTN_MIDDLE, 1, time);
    else if (button == 1)
        queueEvent(touchEvents, K_EV_KEY, K_BTN_RIGHT, 1, time);
    else
        return;
    postSendEvent(touchEvents, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(touchEvents->bufferCond);
}

void onMouseWheel(S32 value) {
    if (!touchEvents) {
        return;
    }
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(touchEvents->bufferCond);

    // Up direction (y > 0)
    if (value > 0)  {
        // Mouse wheel cannot really be held down so the 'up' event is sent, too
        queueEvent(touchEvents, K_EV_KEY, K_BTN_MOUSEWHEEL_UP, value, time);
        queueEvent(touchEvents, K_EV_KEY, K_BTN_MOUSEWHEEL_UP, 0, time);
    }
    // Down direction
    else if (value < 0) {
        queueEvent(touchEvents, K_EV_KEY, K_BTN_MOUSEWHEEL_DOWN, -value, time);
        queueEvent(touchEvents, K_EV_KEY, K_BTN_MOUSEWHEEL_DOWN, 0, time);
    }
    else
        return;

    postSendEvent(touchEvents, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(touchEvents->bufferCond);
}

void onMouseMove(U32 x, U32 y, bool relative) {
    U32 send = 0;
    U64 time = KSystem::getSystemTimeAsMicroSeconds();

    if (relative) {
        if (mouseEvents) {   
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(mouseEvents->bufferCond);
            if (x) {
                queueEvent(mouseEvents, K_EV_REL, K_REL_X, x, time);
                send = 1;
            }
            if (y) {
                queueEvent(mouseEvents, K_EV_REL, K_REL_Y, y, time);
                send = 1;
            }                        
            if (send) {
                postSendEvent(mouseEvents, time);
                BOXEDWINE_CONDITION_SIGNAL_ALL(mouseEvents->bufferCond);
            }
        }
    }
    else {
        if (touchEvents) {
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(touchEvents->bufferCond);
            // :TODO this is a huge hack, for some reason xorg only picks up the first event so
            // to make the mouse mostly smooth, just alternate which axis is first in the queue
            static int count = 0;
            count++;
            if (count % 2 == 0) {
                if (x != touchEvents->lastX) {
                    queueEvent(touchEvents, K_EV_ABS, K_ABS_X, x, time);
                    touchEvents->lastX = x;
                    send = 1;
                }
                if (y != touchEvents->lastY) {
                    queueEvent(touchEvents, K_EV_ABS, K_ABS_Y, y, time);
                    touchEvents->lastY = y;
                    send = 1;
                }
            } else {
                if (y != touchEvents->lastY) {
                    queueEvent(touchEvents, K_EV_ABS, K_ABS_Y, y, time);
                    touchEvents->lastY = y;
                    send = 1;
                }
                if (x != touchEvents->lastX) {
                    queueEvent(touchEvents, K_EV_ABS, K_ABS_X, x, time);
                    touchEvents->lastX = x;
                    send = 1;
                }
            }
            if (send) {
                postSendEvent(touchEvents, time);
                BOXEDWINE_CONDITION_SIGNAL_ALL(touchEvents->bufferCond);
            }
        }        
    }
}

void onKeyDown(U32 code) {
    if (!keyboardEvents) {
        return;
    }
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(keyboardEvents->bufferCond);

    if (code == 0)
        return;
    queueEvent(keyboardEvents, K_EV_KEY, code, 1, time);
    postSendEvent(keyboardEvents, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(keyboardEvents->bufferCond);
}

void onKeyUp(U32 code) {
    if (!keyboardEvents) {
        return;
    }
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(keyboardEvents->bufferCond);

    if (code == 0)
        return;
    queueEvent(keyboardEvents, K_EV_KEY, code, 0, time);
    postSendEvent(keyboardEvents, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(keyboardEvents->bufferCond);
}
