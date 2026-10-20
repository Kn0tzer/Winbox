#include "boxedwine.h"

#ifdef __TEST

#include "testGamepadInput.h"
#include "../cpu/testCPU.h"

#include <memory>

FsOpenNode* openDevInputGamepad(const std::shared_ptr<FsNode>& node, U32 flags, U32 data);
void onGamepadAxis(U32 code, S32 value);
void onGamepadButton(U32 code, bool down);
void onGamepadSync();

namespace {

constexpr U32 GAMEPAD_READ_BUFFER = TEST_HEAP_ADDRESS + 0x1000;
constexpr U32 GAMEPAD_IO_BUFFER = TEST_HEAP_ADDRESS + 0x1200;

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

    // These are the host-to-evdev callbacks used by the SDL controller handler.
    onGamepadAxis(0x00, 32767);
    onGamepadSync();
    onGamepadButton(0x130, true);
    onGamepadSync();
    onGamepadAxis(0x05, 255);
    onGamepadSync();
    onGamepadAxis(0x11, -1);
    onGamepadSync();

    U32 bytesRead = device->readNative(records, sizeof(records));
    expectGamepadValue("gamepad input record bytes", bytesRead, sizeof(records));
    const U32* words = reinterpret_cast<const U32*>(records);
    const U32 expectedCodes[] = {0x00000003, 0x00000000, 0x01300001, 0x00000000,
                                 0x00050003, 0x00000000, 0x00110003, 0x00000000};
    const S32 expectedValues[] = {32767, 0, 1, 0, 255, 0, -1, 0};
    for (U32 i = 0; i < 8; i++) {
        expectGamepadValue("gamepad event code", words[i * 4 + 2], expectedCodes[i]);
        expectGamepadValue("gamepad event value", words[i * 4 + 3], (U32)expectedValues[i]);
        if (i && (words[i * 4] < words[(i - 1) * 4] ||
                  (words[i * 4] == words[(i - 1) * 4] && words[i * 4 + 1] < words[(i - 1) * 4 + 1]))) {
            testFail("gamepad event timestamps went backwards");
        }
    }

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

    expectGamepadValue("unknown gamepad ioctl", gamepadIoctl(device.get(), context, 0xdead), (U32)-K_EINVAL);

    onGamepadButton(0x130, false);
    onGamepadSync();
}

#endif
