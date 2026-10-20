#include "boxedwine.h"
#include "kmemory64.h"

#ifdef __TEST

#include "testGamepadInput.h"
#include "../cpu/testCPU.h"

#include <memory>

FsOpenNode* openDevInputGamepad(const std::shared_ptr<FsNode>& node, U32 flags, U32 data);
void onGamepadState(int connected, int buttons, int lx, int ly, int rx, int ry, int lt, int rt, int hatx, int haty);

namespace {

constexpr U32 GAMEPAD_READ_BUFFER = TEST_HEAP_ADDRESS + 0x1000;
constexpr U32 GAMEPAD_IO_BUFFER = TEST_HEAP_ADDRESS + 0x1200;
#ifdef BOXEDWINE_GUEST_X64
constexpr U64 GAMEPAD64_IO_BUFFER = 0x100002000ULL;
#endif

class TestGamepadNode : public FsNode {
public:
    TestGamepadNode() : FsNode(Type::Virtual, 5, 0, B("/dev/input/event5"), B(""), B(""), false, nullptr) {}
    U32 rename(BString) override { return 0; }
    bool remove() override { return false; }
    U64 lastModified() override { return 0; }
    U64 length() override { return 0; }
    FsOpenNode* open(U32) override { return nullptr; }
    U32 getType(bool) override { return 0; }
    U32 getMode() override { return 0; }
    U32 removeDir() override { return 0; }
    U32 setTimes(U64, U32, U64, U32) override { return 0; }
};

void expectGamepadValue(const char* label, U32 actual, U32 expected) {
    if (actual != expected) {
        testFail("%s expected %u (0x%X), got %u (0x%X)", label, expected, expected, actual, actual);
    }
}

U32 gamepadIoctl(FsOpenNode* device, TestContext& context, U32 request) {
    CPU* cpu = context.cpu;
    EDX = GAMEPAD_IO_BUFFER;
    context.memory->memset(GAMEPAD_IO_BUFFER, 0, 128);
    return device->ioctl(context.thread, request);
}

#ifdef BOXEDWINE_GUEST_X64
U32 gamepadIoctl64(FsOpenNode* device, KMemory64* memory, U32 request) {
    memory->memsetGuest(GAMEPAD64_IO_BUFFER, 0, 128);
    return device->ioctl64(request, GAMEPAD64_IO_BUFFER, memory);
}
#endif

U32 inputIoctl(U32 command, U32 len) {
    return 0x80000000 | (len << 16) | command;
}

} // namespace

void testGamepadInput() {
    TestContext& context = testContext();
    std::shared_ptr<FsNode> node = std::make_shared<TestGamepadNode>();
    std::unique_ptr<FsOpenNode> device(openDevInputGamepad(node, 0, 0));

    U8 records[8 * 16] = {};
    expectGamepadValue("idle event5 read", device->readNative(records, 16), (U32)-K_EWOULDBLOCK);

    // A browser poll delivers one snapshot; changed key and axis events share
    // one SYN_REPORT and an unchanged snapshot produces no records.
    onGamepadState(1, 1 << 0, 32767, 0, 0, 0, 0, 255, 0, -1);

    U32 bytesRead = device->readNative(records, sizeof(records));
    expectGamepadValue("gamepad input record bytes", bytesRead, 5 * 16);
    const U32* words = reinterpret_cast<const U32*>(records);
    const U32 expectedCodes[] = {0x01300001, 0x00000003, 0x00050003, 0x00110003, 0x00000000};
    const S32 expectedValues[] = {1, 32767, 255, -1, 0};
    for (U32 i = 0; i < 5; i++) {
        expectGamepadValue("gamepad event code", words[i * 4 + 2], expectedCodes[i]);
        expectGamepadValue("gamepad event value", words[i * 4 + 3], (U32)expectedValues[i]);
        if (i && (words[i * 4] < words[(i - 1) * 4] ||
                  (words[i * 4] == words[(i - 1) * 4] && words[i * 4 + 1] < words[(i - 1) * 4 + 1]))) {
            testFail("gamepad event timestamps went backwards");
        }
    }
    onGamepadState(1, 1 << 0, 32767, 0, 0, 0, 0, 255, 0, -1);
    expectGamepadValue("unchanged gamepad snapshot", device->readNative(records, 16), (U32)-K_EWOULDBLOCK);

    expectGamepadValue("EVIOCGVERSION return", gamepadIoctl(device.get(), context, inputIoctl(0x4501, 4)), 4);
    expectGamepadValue("EVIOCGVERSION value", context.memory->readd(GAMEPAD_IO_BUFFER), 0x010001);
    expectGamepadValue("EVIOCGID return", gamepadIoctl(device.get(), context, inputIoctl(0x4502, 8)), 0);
    expectGamepadValue("EVIOCGID bustype", context.memory->readw(GAMEPAD_IO_BUFFER), 3);
    expectGamepadValue("EVIOCGID vendor", context.memory->readw(GAMEPAD_IO_BUFFER + 2), 0x045e);
    expectGamepadValue("EVIOCGID product", context.memory->readw(GAMEPAD_IO_BUFFER + 4), 0x028e);
    expectGamepadValue("EVIOCGID version", context.memory->readw(GAMEPAD_IO_BUFFER + 6), 0x0114);

    expectGamepadValue("EVIOCGNAME return", gamepadIoctl(device.get(), context, inputIoctl(0x4506, 64)), sizeof("Microsoft X-Box 360 pad"));
    if (strcmp(context.memory->readString(GAMEPAD_IO_BUFFER).c_str(), "Microsoft X-Box 360 pad") != 0) {
        testFail("EVIOCGNAME did not return the Xbox 360 pad name");
    }
    gamepadIoctl(device.get(), context, inputIoctl(0x4507, 64));
    if (strcmp(context.memory->readString(GAMEPAD_IO_BUFFER).c_str(), "usb-boxedwine/input0") != 0) {
        testFail("EVIOCGPHYS did not return usb-boxedwine/input0");
    }
    gamepadIoctl(device.get(), context, inputIoctl(0x4508, 64));
    expectGamepadValue("EVIOCGUNIQ", context.memory->readb(GAMEPAD_IO_BUFFER), 0);
    expectGamepadValue("EVIOCGPROP", gamepadIoctl(device.get(), context, inputIoctl(0x4509, 4)), 4);
    expectGamepadValue("EVIOCGPROP value", context.memory->readd(GAMEPAD_IO_BUFFER), 0);

    gamepadIoctl(device.get(), context, inputIoctl(0x4520, 4));
    expectGamepadValue("EVIOCG event types", context.memory->readb(GAMEPAD_IO_BUFFER), 0x0b);
    gamepadIoctl(device.get(), context, inputIoctl(0x4521, 40));
    expectGamepadValue("EVIOCGKEY capability BTN_A", context.memory->readb(GAMEPAD_IO_BUFFER + 0x130 / 8) & (1 << (0x130 % 8)), 1);
    gamepadIoctl(device.get(), context, inputIoctl(0x4518, 40));
    expectGamepadValue("EVIOCGKEY current BTN_A", context.memory->readb(GAMEPAD_IO_BUFFER + 0x130 / 8) & (1 << (0x130 % 8)), 1);
    expectGamepadValue("EVIOCGBIT(EV_REL) return", gamepadIoctl(device.get(), context, inputIoctl(0x4522, 8)), 0);
    expectGamepadValue("EVIOCGBIT(EV_REL) absent", context.memory->readd(GAMEPAD_IO_BUFFER), 0);
    gamepadIoctl(device.get(), context, inputIoctl(0x4523, 3));
    expectGamepadValue("EVIOCGABS capability byte 0", context.memory->readb(GAMEPAD_IO_BUFFER), 0x3f);
    expectGamepadValue("EVIOCGABS capability byte 2", context.memory->readb(GAMEPAD_IO_BUFFER + 2), 0x03);
    expectGamepadValue("EVIOCGBIT(EV_FF) absent", gamepadIoctl(device.get(), context, inputIoctl(0x4535, 8)), 0);
    expectGamepadValue("EVIOCGBIT(EV_FF) value", context.memory->readd(GAMEPAD_IO_BUFFER), 0);
    expectGamepadValue("EVIOCGRAB", gamepadIoctl(device.get(), context, 0x40044590), 0);

    const U32 absCodes[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x10, 0x11};
    const S32 absMinimums[] = {-32768, -32768, 0, -32768, -32768, 0, -1, -1};
    const S32 absMaximums[] = {32767, 32767, 255, 32767, 32767, 255, 1, 1};
    const S32 absFuzz[] = {16, 16, 0, 16, 16, 0, 0, 0};
    const S32 absFlat[] = {128, 128, 0, 128, 128, 0, 0, 0};
    const S32 absValues[] = {32767, 0, 0, 0, 0, 255, 0, -1};
    for (U32 i = 0; i < 8; i++) {
        gamepadIoctl(device.get(), context, inputIoctl(0x4540 + absCodes[i], 24));
        expectGamepadValue("EVIOCGABS current", context.memory->readd(GAMEPAD_IO_BUFFER), (U32)absValues[i]);
        expectGamepadValue("EVIOCGABS minimum", context.memory->readd(GAMEPAD_IO_BUFFER + 4), (U32)absMinimums[i]);
        expectGamepadValue("EVIOCGABS maximum", context.memory->readd(GAMEPAD_IO_BUFFER + 8), (U32)absMaximums[i]);
        expectGamepadValue("EVIOCGABS fuzz", context.memory->readd(GAMEPAD_IO_BUFFER + 12), (U32)absFuzz[i]);
        expectGamepadValue("EVIOCGABS flat", context.memory->readd(GAMEPAD_IO_BUFFER + 16), (U32)absFlat[i]);
        expectGamepadValue("EVIOCGABS resolution", context.memory->readd(GAMEPAD_IO_BUFFER + 20), 0);
    }

#ifdef BOXEDWINE_GUEST_X64
    // The x86-64 syscall path supplies a full-width ioctl pointer through
    // FsOpenNode::ioctl64. Keep the buffer above 4 GiB to catch accidental
    // truncation back to the 32-bit KMemory path.
    KMemory64 memory64(nullptr);
    expectGamepadValue("64-bit EVIOCGID return", gamepadIoctl64(device.get(), &memory64, inputIoctl(0x4502, 8)), 0);
    expectGamepadValue("64-bit EVIOCGID vendor", memory64.readw(GAMEPAD64_IO_BUFFER + 2), 0x045e);
    expectGamepadValue("64-bit EVIOCGID product", memory64.readw(GAMEPAD64_IO_BUFFER + 4), 0x028e);
    expectGamepadValue("64-bit EVIOCGNAME return", gamepadIoctl64(device.get(), &memory64, inputIoctl(0x4506, 64)), sizeof("Microsoft X-Box 360 pad"));
    char gamepadName[64] = {};
    memory64.memcpyFromGuest(gamepadName, GAMEPAD64_IO_BUFFER, sizeof(gamepadName));
    if (strcmp(gamepadName, "Microsoft X-Box 360 pad") != 0) {
        testFail("64-bit EVIOCGNAME did not return the Xbox 360 pad name");
    }
    expectGamepadValue("64-bit EVIOCGBIT(EV_SYN) return", gamepadIoctl64(device.get(), &memory64, inputIoctl(0x4520, 8)), 1);
    expectGamepadValue("64-bit EVIOCGBIT(EV_SYN) event types", memory64.readb(GAMEPAD64_IO_BUFFER), 0x0b);
    expectGamepadValue("64-bit EVIOCGBIT(EV_KEY) return", gamepadIoctl64(device.get(), &memory64, inputIoctl(0x4521, 96)), 40);
    expectGamepadValue("64-bit EVIOCGBIT(EV_KEY) BTN_A", memory64.readb(GAMEPAD64_IO_BUFFER + 0x130 / 8) & 1, 1);
    expectGamepadValue("64-bit EVIOCGBIT(EV_REL) return", gamepadIoctl64(device.get(), &memory64, inputIoctl(0x4522, 8)), 0);
    expectGamepadValue("64-bit EVIOCGBIT(EV_REL) absent", memory64.readd(GAMEPAD64_IO_BUFFER), 0);
    expectGamepadValue("64-bit EVIOCGBIT(EV_ABS) return", gamepadIoctl64(device.get(), &memory64, inputIoctl(0x4523, 8)), 3);
    expectGamepadValue("64-bit EVIOCGBIT(EV_ABS) axes", memory64.readb(GAMEPAD64_IO_BUFFER), 0x3f);
    expectGamepadValue("64-bit EVIOCGBIT(EV_ABS) hats", memory64.readb(GAMEPAD64_IO_BUFFER + 2), 3);
    expectGamepadValue("64-bit EVIOCGABS return", gamepadIoctl64(device.get(), &memory64, inputIoctl(0x4540, 24)), 0);
    expectGamepadValue("64-bit EVIOCGABS current", memory64.readd(GAMEPAD64_IO_BUFFER), (U32)32767);
    expectGamepadValue("64-bit EVIOCGABS minimum", memory64.readd(GAMEPAD64_IO_BUFFER + 4), (U32)-32768);
    expectGamepadValue("64-bit unknown gamepad ioctl", device->ioctl64(0xdead, GAMEPAD64_IO_BUFFER, &memory64), (U32)-K_EINVAL);
#endif

    expectGamepadValue("unknown gamepad ioctl", gamepadIoctl(device.get(), context, 0xdead), (U32)-K_EINVAL);

    onGamepadState(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    bytesRead = device->readNative(records, sizeof(records));
    expectGamepadValue("disconnect event record bytes", bytesRead, 5 * 16);
    const U32 disconnectCodes[] = {0x01300001, 0x00000003, 0x00050003, 0x00110003, 0x00000000};
    const S32 disconnectValues[] = {0, 0, 0, 0, 0};
    for (U32 i = 0; i < 5; i++) {
        expectGamepadValue("disconnect event code", words[i * 4 + 2], disconnectCodes[i]);
        expectGamepadValue("disconnect event value", words[i * 4 + 3], (U32)disconnectValues[i]);
    }
}

#endif
