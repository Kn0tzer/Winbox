#include "boxedwine.h"
#ifdef __TEST
#include "testCPU.h"
#include "ksignal.h"
#include "testCpuSystem.h"

namespace {
void emit(const U8* code, size_t size) {
    for (size_t i = 0; i < size; ++i) testPushCode8(code[i]);
}

void runTrapCase(const char* name, const U8* code, size_t size, U32 trap, U32 initialFlags = 0) {
    auto& context = testContext();
    testNewInstruction(initialFlags);
    context.cpu->reg[0].u32 = 0x100;
    KSigAction& action = context.process->sigActions[K_SIGSEGV];
    action.reset();
    action.handlerAndSigAction = TEST_CODE_ADDRESS + 0x100;
    action.flags = K_SA_SIGINFO;
    context.memory->writeb(TEST_CODE_ADDRESS + 0x100, 0xcd);
    context.memory->writeb(TEST_CODE_ADDRESS + 0x101, 0x97);
    emit(code, size);
    testRunCPU();
    if (action.sigInfo[0] != K_SIGSEGV || action.sigInfo[4] != trap)
        testFail("%s expected SIGSEGV trap %u, got signal=%u trap=%u", name, trap, action.sigInfo[0], action.sigInfo[4]);
}

}

void testCpuSystemInstructions() {
    auto& context = testContext();
    static const U8 sgdt[] = {0x0f, 0x01, 0x00};
    context.cpu->reg[0].u32 = 0x100;
    testNewInstruction(0);
    context.cpu->reg[0].u32 = 0x100;
    emit(sgdt, sizeof(sgdt)); testRunCPU();
    if (context.memory->readw(TEST_HEAP_ADDRESS + 0x100) != 0x7f || context.memory->readd(TEST_HEAP_ADDRESS + 0x102) != 0)
        testFail("SGDT memory value");

    static const U8 sidt[] = {0x0f, 0x01, 0x08};
    testNewInstruction(0); context.cpu->reg[0].u32 = 0x100;
    emit(sidt, sizeof(sidt)); testRunCPU();
    if (context.memory->readw(TEST_HEAP_ADDRESS + 0x100) != 1023 || context.memory->readd(TEST_HEAP_ADDRESS + 0x102) != 0)
        testFail("SIDT memory value");

    static const U8 sldtReg[] = {0x0f, 0x00, 0xc0};
    testNewInstruction(0); context.cpu->reg[0].u32 = 0x12345678;
    emit(sldtReg, sizeof(sldtReg)); testRunCPU();
    if (context.cpu->reg[0].u32 != 0x12340000) testFail("SLDT register value");
    static const U8 sldtMem[] = {0x0f, 0x00, 0x00};
    testNewInstruction(0); context.cpu->reg[0].u32 = 0x100;
    emit(sldtMem, sizeof(sldtMem)); testRunCPU();
    if (context.memory->readw(TEST_HEAP_ADDRESS + 0x100) != 0) testFail("SLDT memory value");

    static const U8 strReg[] = {0x0f, 0x00, 0xc8};
    testNewInstruction(0); context.cpu->reg[0].u32 = 0x12345678;
    emit(strReg, sizeof(strReg)); testRunCPU();
    if (context.cpu->reg[0].u32 != 0x12340000) testFail("STR register value");
    static const U8 strMem[] = {0x0f, 0x00, 0x08};
    testNewInstruction(0); context.cpu->reg[0].u32 = 0x100;
    emit(strMem, sizeof(strMem)); testRunCPU();
    if (context.memory->readw(TEST_HEAP_ADDRESS + 0x100) != 0) testFail("STR memory value");

    static const U8 smsw32[] = {0x0f, 0x01, 0xe0};
    testNewInstruction(0); context.cpu->reg[0].u32 = 0x12345678;
    emit(smsw32, sizeof(smsw32)); testRunCPU();
    if (context.cpu->reg[0].u32 != 0x80050033) testFail("SMSW 32-bit register expected 80050033 got %08x", context.cpu->reg[0].u32);
    static const U8 smsw16[] = {0x66, 0x0f, 0x01, 0xe0};
    testNewInstruction(0); context.cpu->reg[0].u32 = 0x12345678;
    emit(smsw16, sizeof(smsw16)); testRunCPU();
    if (context.cpu->reg[0].u32 != 0x12340033) testFail("SMSW 16-bit register value");
    static const U8 smswMem[] = {0x0f, 0x01, 0x20};
    testNewInstruction(0); context.cpu->reg[0].u32 = 0x100;
    emit(smswMem, sizeof(smswMem)); testRunCPU();
    if (context.memory->readw(TEST_HEAP_ADDRESS + 0x100) != 0x33) testFail("SMSW memory value");

    static const U8 lgdt[] = {0x0f, 0x01, 0x10};
    static const U8 lidt[] = {0x0f, 0x01, 0x18};
    static const U8 lmsw[] = {0x0f, 0x01, 0x30};
    static const U8 invlpg[] = {0x0f, 0x01, 0x38};
    static const U8 lldt[] = {0x0f, 0x00, 0x10};
    static const U8 ltr[] = {0x0f, 0x00, 0x18};
    runTrapCase("LGDT", lgdt, sizeof(lgdt), 13);
    runTrapCase("LIDT", lidt, sizeof(lidt), 13);
    runTrapCase("LMSW", lmsw, sizeof(lmsw), 13);
    runTrapCase("INVLPG", invlpg, sizeof(invlpg), 13);
    runTrapCase("LLDT", lldt, sizeof(lldt), 13);
    runTrapCase("LTR", ltr, sizeof(ltr), 13);

    static const U8 into[] = {0xce};
    testNewInstruction(0); emit(into, sizeof(into)); testRunCPU();
    if (context.cpu->getOF()) testFail("INTO cleared OF unexpectedly");
    runTrapCase("INTO", into, sizeof(into), 4, OF);
}
#endif
