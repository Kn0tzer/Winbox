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

#include <array>
#include <string.h>

#include "../../io/fsvirtualopennode.h"

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

#define GAMEPAD_BTN_A           0x130
#define GAMEPAD_BTN_B           0x131
#define GAMEPAD_BTN_X           0x133
#define GAMEPAD_BTN_Y           0x134
#define GAMEPAD_BTN_TL          0x136
#define GAMEPAD_BTN_TR          0x137
#define GAMEPAD_BTN_SELECT      0x13a
#define GAMEPAD_BTN_START       0x13b
#define GAMEPAD_BTN_MODE        0x13c
#define GAMEPAD_BTN_THUMBL      0x13d
#define GAMEPAD_BTN_THUMBR      0x13e
#define GAMEPAD_ABS_X           0x00
#define GAMEPAD_ABS_Y           0x01
#define GAMEPAD_ABS_Z           0x02
#define GAMEPAD_ABS_RX          0x03
#define GAMEPAD_ABS_RY          0x04
#define GAMEPAD_ABS_RZ          0x05
#define GAMEPAD_ABS_HAT0X       0x10
#define GAMEPAD_ABS_HAT0Y       0x11

static const U32 gamepadButtons[] = {
    GAMEPAD_BTN_A, GAMEPAD_BTN_B, GAMEPAD_BTN_X, GAMEPAD_BTN_Y,
    GAMEPAD_BTN_TL, GAMEPAD_BTN_TR, GAMEPAD_BTN_SELECT, GAMEPAD_BTN_START,
    GAMEPAD_BTN_MODE, GAMEPAD_BTN_THUMBL, GAMEPAD_BTN_THUMBR
};

struct GamepadAxisInfo {
    U32 code;
    S32 min;
    S32 max;
    S32 fuzz;
    S32 flat;
};

static const GamepadAxisInfo gamepadAxes[] = {
    {GAMEPAD_ABS_X, -32768, 32767, 16, 128},
    {GAMEPAD_ABS_Y, -32768, 32767, 16, 128},
    {GAMEPAD_ABS_Z, 0, 255, 0, 0},
    {GAMEPAD_ABS_RX, -32768, 32767, 16, 128},
    {GAMEPAD_ABS_RY, -32768, 32767, 16, 128},
    {GAMEPAD_ABS_RZ, 0, 255, 0, 0},
    {GAMEPAD_ABS_HAT0X, -1, 1, 0, 0},
    {GAMEPAD_ABS_HAT0Y, -1, 1, 0, 0}
};

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

class DevInputGamepad : public DevInput {
public:
    DevInputGamepad(const std::shared_ptr<FsNode>& node, U32 flags);

    U32 ioctl(KThread* thread, U32 request) override;

    std::array<S32, 0x12> axes{};
    std::array<bool, GAMEPAD_BTN_THUMBR + 1> buttons{};
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

static DevInputGamepad* gamepadEvents;

FsOpenNode* openDevInputGamepad(const std::shared_ptr<FsNode>& node, U32 flags, U32 data) {
    gamepadEvents = new DevInputGamepad(node, flags);
    gamepadEvents->clearOnExit = (DevInput**)&gamepadEvents;
    return gamepadEvents;
}

// :TODO: can this be blocking
U32 DevInput::readNative(U8* buffer, U32 len) {
    U32 result = 0;
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->bufferCond);
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

static void writeBit(KMemory* memory, U32 address, U32 bit) {
    U32 b = bit/8;
    U32 p = bit % 8;
    U32 value = memory->readb(address+b);
    value|=(1<<p);
    memory->writeb(address+b, value);
}

//    struct input_absinfo {
//        __s32 value;
//        __s32 minimum;
//        __s32 maximum;
//        __s32 fuzz;
//        __s32 flat;
//        __s32 resolution;
//    };
static void writeAbs(KMemory* memory, U32 address, U32 value, U32 min, U32 max) {
    memory->writed(address, value);
    memory->writed(address+4, min);
    memory->writed(address+8, max);
    memory->writed(address+12, 0);
    memory->writed(address+16, 0);
    memory->writed(address+20, 96);
}

U32 DevInput::ioctl(KThread* thread, U32 request) {
    CPU* cpu = thread->cpu;
    U32 cmd = request & 0xFFFF;
    KMemory* memory = thread->memory;

    switch (cmd) {
        case 0x4501: { // EVIOCGVERSION
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
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
            U32 buffer = IOCTL_ARG1;
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
            U32 buffer = IOCTL_ARG1;
            U32 todo = std::min((U32)name.length(), len);
            memory->memcpy(buffer, this->name.c_str(), todo);
            return todo;
        }
       case 0x4507: { // EVIOCGPHYS
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            U32 todo = std::min((U32)name.length(), len);
            memory->memcpy(buffer, this->name.c_str(), todo);
            return todo;
        }
        case 0x4508: { //  EVIOCGUNIQ
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            U32 todo = std::min((U32)name.length(), len);
            memory->memcpy(buffer, this->name.c_str(), todo);
            return todo;
        }
        case 0x4509: { // EVIOCGPROP
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            if (len<4)
                kpanic_fmt("Bad length for EVIOCGBIT: %d", len);
            memory->writed(buffer, this->prop);
            return 4;
        }
        case 0x4518: { // EVIOCGKEY
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 0;
        }
        case 0x4519: { // EVIOCGLED
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 0;
        }
        case 0x451b: { // EVIOCGSW
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 0;
        }
        case 0x4520: { // EVIOCGBIT
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            if (len<4)
                kpanic_fmt("Bad length for EVIOCGBIT: %d", len);
            memory->writed(buffer, this->mask);
            return 4;
        }
        case 0x4524: { // EVIOCGBIT(EV_MSC)
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 0;
        }
        case 0x4525: { // EVIOCGBIT(EV_SW) - Switches
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 0;
        }
        case 0x4532: { // EVIOCGBIT(EV_SND) - Sound Effects
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 0;
        }
        case 0x4535: { // EVIOCGBIT(EV_FF) - Force Feedback
            U32 len = (request & 0x1fff0000) >> 16;
            U32 buffer = IOCTL_ARG1;
            memory->memset(buffer, 0, len);
            return 0;
        }
        case 0x540B: // TCFLSH
            return 0;		
    }
    return -1;
}

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

DevInputGamepad::DevInputGamepad(const std::shared_ptr<FsNode>& node, U32 flags) : DevInput(node, flags) {
    this->bustype = 0x03;
    this->vendor = 0x045e;
    this->product = 0x028e;
    this->version = 0x0114;
    this->name = B("Microsoft X-Box 360 pad");
    this->mask = (1 << K_EV_SYN) | (1 << K_EV_KEY) | (1 << K_EV_ABS);
}

static U32 copyGamepadString(KMemory* memory, U32 address, U32 len, const char* value) {
    U32 copyLen = std::min((U32)strlen(value) + 1, len);
    if (copyLen) {
        memory->memcpy(address, value, copyLen);
    }
    return copyLen;
}

static bool getGamepadAxisInfo(U32 code, GamepadAxisInfo& info) {
    for (const GamepadAxisInfo& candidate : gamepadAxes) {
        if (candidate.code == code) {
            info = candidate;
            return true;
        }
    }
    return false;
}

static U32 writeGamepadBitmap(KMemory* memory, U32 address, U32 len, const U32* codes, U32 count) {
    memory->memset(address, 0, len);
    U32 lastBit = 0;
    for (U32 i = 0; i < count; i++) {
        if (codes[i] / 8 < len) {
            writeBit(memory, address, codes[i]);
        }
        lastBit = std::max(lastBit, codes[i]);
    }
    return (lastBit + 8) / 8;
}

U32 DevInputGamepad::ioctl(KThread* thread, U32 request) {
    CPU* cpu = thread->cpu;
    KMemory* memory = thread->memory;
    U32 cmd = request & 0xffff;
    U32 len = (request & 0x1fff0000) >> 16;
    U32 buffer = IOCTL_ARG1;

    switch (cmd) {
    case 0x4501: // EVIOCGVERSION
        if (len < 4) return -K_EINVAL;
        memory->writed(buffer, 0x010001);
        return 4;
    case 0x4502: // EVIOCGID
        if (len < 8) return -K_EINVAL;
        memory->writew(buffer, this->bustype);
        memory->writew(buffer + 2, this->vendor);
        memory->writew(buffer + 4, this->product);
        memory->writew(buffer + 6, this->version);
        return 0;
    case 0x4506: // EVIOCGNAME
        return copyGamepadString(memory, buffer, len, "Microsoft X-Box 360 pad");
    case 0x4507: // EVIOCGPHYS
        return copyGamepadString(memory, buffer, len, "usb-boxedwine/input0");
    case 0x4508: // EVIOCGUNIQ
        if (len) memory->writeb(buffer, 0);
        return 0;
    case 0x4509: // EVIOCGPROP
        memory->memset(buffer, 0, len);
        return std::min(len, 4u);
    case 0x4518: { // EVIOCGKEY
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->bufferCond);
        memory->memset(buffer, 0, len);
        for (U32 code : gamepadButtons) {
            if (this->buttons[code] && code / 8 < len) {
                writeBit(memory, buffer, code);
            }
        }
        return 0;
    }
    case 0x4520: { // EVIOCGBIT(EV_SYN)
        static const U32 eventTypes[] = {K_EV_SYN, K_EV_KEY, K_EV_ABS};
        return writeGamepadBitmap(memory, buffer, len, eventTypes, 3);
    }
    case 0x4521: // EVIOCGBIT(EV_KEY)
        return writeGamepadBitmap(memory, buffer, len, gamepadButtons, (U32)(sizeof(gamepadButtons) / sizeof(gamepadButtons[0])));
    case 0x4523: { // EVIOCGBIT(EV_ABS)
        static const U32 axes[] = {GAMEPAD_ABS_X, GAMEPAD_ABS_Y, GAMEPAD_ABS_Z, GAMEPAD_ABS_RX,
                                   GAMEPAD_ABS_RY, GAMEPAD_ABS_RZ, GAMEPAD_ABS_HAT0X, GAMEPAD_ABS_HAT0Y};
        return writeGamepadBitmap(memory, buffer, len, axes, (U32)(sizeof(axes) / sizeof(axes[0])));
    }
    case 0x4535: // EVIOCGBIT(EV_FF): no force-feedback capabilities
        memory->memset(buffer, 0, len);
        return 0;
    case 0x4590: // EVIOCGRAB
        return 0;
    default:
        if (cmd >= 0x4540 && cmd <= 0x4551) { // EVIOCGABS(axis)
            U32 axis = cmd - 0x4540;
            GamepadAxisInfo info;
            if (!getGamepadAxisInfo(axis, info) || len < 24) return -K_EINVAL;
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->bufferCond);
            memory->writed(buffer, (U32)this->axes[axis]);
            memory->writed(buffer + 4, (U32)info.min);
            memory->writed(buffer + 8, (U32)info.max);
            memory->writed(buffer + 12, (U32)info.fuzz);
            memory->writed(buffer + 16, (U32)info.flat);
            memory->writed(buffer + 20, 0);
            return 0;
        }
        return -K_EINVAL;
    }
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

void onGamepadAxis(U32 code, S32 value) {
    if (!gamepadEvents || code >= gamepadEvents->axes.size()) return;
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(gamepadEvents->bufferCond);
    gamepadEvents->axes[code] = value;
    queueEvent(gamepadEvents, K_EV_ABS, code, (U32)value, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(gamepadEvents->bufferCond);
}

void onGamepadButton(U32 code, bool down) {
    if (!gamepadEvents || code >= gamepadEvents->buttons.size()) return;
    bool supported = false;
    for (U32 candidate : gamepadButtons) {
        if (candidate == code) {
            supported = true;
            break;
        }
    }
    if (!supported) return;
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(gamepadEvents->bufferCond);
    gamepadEvents->buttons[code] = down;
    queueEvent(gamepadEvents, K_EV_KEY, code, down ? 1 : 0, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(gamepadEvents->bufferCond);
}

void onGamepadSync() {
    if (!gamepadEvents) return;
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(gamepadEvents->bufferCond);
    postSendEvent(gamepadEvents, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(gamepadEvents->bufferCond);
}

void onGamepadDisconnected() {
    if (!gamepadEvents) return;
    U64 time = KSystem::getSystemTimeAsMicroSeconds();
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(gamepadEvents->bufferCond);
    for (U32 code : gamepadButtons) {
        if (gamepadEvents->buttons[code]) {
            gamepadEvents->buttons[code] = false;
            queueEvent(gamepadEvents, K_EV_KEY, code, 0, time);
        }
    }
    for (const GamepadAxisInfo& info : gamepadAxes) {
        if (gamepadEvents->axes[info.code]) {
            gamepadEvents->axes[info.code] = 0;
            queueEvent(gamepadEvents, K_EV_ABS, info.code, 0, time);
        }
    }
    postSendEvent(gamepadEvents, time);
    BOXEDWINE_CONDITION_SIGNAL_ALL(gamepadEvents->bufferCond);
}
