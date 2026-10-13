// GPL-2.0-or-later.
#include "boxedwine.h"
#ifdef __TEST
#include "testCPU.h"
#include "../../emulation/cpu/decoder.h"
#include "testDecoderPageBoundary.h"

namespace {
struct PageBoundary : DecodeBlockCallback {
    U8 bytes[16]{};
    U32 start = 0x2000 - 1;
    bool fetchedNonExecutable = false;
    U8 fetchByte(U32* eip) override {
        if (*eip >= 0x2000) fetchedNonExecutable = true;
        return bytes[(*eip)++ - start];
    }
    bool shouldContinue(U32) override { return true; }
    DecodedOp** getOpLocation(U32) override { return nullptr; }
    DecodedOp* getOp(U32, U32) override { return nullptr; }
    bool isValidExecutableAddress(U32 address) override { return address < 0x2000; }
};
}

void testDecoderPageBoundary() {
    PageBoundary boundary;
    boundary.bytes[0] = 0xc3; // RET: its final byte is executable.
    U32 count = 0, length = 0;
    DecodedOp* op = decodeBlock(&boundary, boundary.start, true, count, length);
    if (!op || count != 1 || length != 1 || boundary.fetchedNonExecutable)
        testFail("RET ending at an executable page boundary must decode (count=%u len=%u)", count, length);
    if (op) op->dealloc();

    boundary.start = 0x2000 - 5;
    boundary.bytes[0] = 0xb8; // MOV EAX, imm32, exactly five bytes.
    op = decodeBlock(&boundary, boundary.start, true, count, length);
    if (!op || count != 1 || length != 5 || boundary.fetchedNonExecutable)
        testFail("MOV ending at an executable page boundary must decode (count=%u len=%u)", count, length);
    if (op) op->dealloc();

    boundary.start = 0x2000 - 4; // final MOV byte is on a non-executable page.
    op = decodeBlock(&boundary, boundary.start, true, count, length);
    if (op) { testFail("An instruction crossing into a non-executable page must fail"); op->dealloc(); }
}
#endif
