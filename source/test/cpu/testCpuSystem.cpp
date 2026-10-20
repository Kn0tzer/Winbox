#include "boxedwine.h"
#ifdef __TEST
#include "testCPU.h"
#include "ksignal.h"
#include "testCpuSystem.h"
#include "cpuinfo.h"
#include "../../io/fsopennode.h"
#include "../../io/fsfilenode.h"

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

void testCpuCpuidAndCpuInfo() {
    auto& context = testContext();
    CPU* cpu = context.cpu;
    U32 oldPentiumLevel = KSystem::pentiumLevel;
    KSystem::pentiumLevel = 4;

    auto checkCpuid = [&](U32 leaf, U32 eax, U32 ebx, U32 ecx, U32 edx) {
        cpu->reg[0].u32 = leaf;
        cpu->reg[1].u32 = 0xffffffff;
        cpu->reg[2].u32 = 0xaaaaaaaa;
        cpu->reg[3].u32 = 0x55555555;
        cpu->cpuid();
        if (cpu->reg[0].u32 != eax || cpu->reg[3].u32 != ebx ||
                cpu->reg[1].u32 != ecx || cpu->reg[2].u32 != edx)
            testFail("CPUID leaf %08x expected %08x:%08x:%08x:%08x got %08x:%08x:%08x:%08x",
                leaf, eax, ebx, ecx, edx, cpu->reg[0].u32, cpu->reg[3].u32, cpu->reg[1].u32, cpu->reg[2].u32);
    };

    checkCpuid(0, 7, 0x756e6547, 0x6c65746e, 0x49656e69);
    checkCpuid(1, 0x000006fb, 0x00010800, 0x00980201, 0x0788a131);
    checkCpuid(7, 0, 0, 0, 0);
    checkCpuid(0x80000000, 0x80000004, 0, 0, 0);
    checkCpuid(0x80000001, 0, 0, 0, 0);
    KSystem::pentiumLevel = oldPentiumLevel;

    std::shared_ptr<FsNode> oldProcNode = KSystem::procNode;
    if (!KSystem::procNode)
        KSystem::procNode = std::make_shared<FsFileNode>(0, 0, B("/proc"), B(""), B(""), true, false, nullptr);
    const bool oldIs64Bit = context.process->is64Bit;
    auto readCpuInfo = [&](bool is64Bit) {
        context.process->is64Bit = is64Bit;
        std::unique_ptr<FsOpenNode> file(openCpuInfo(KSystem::procNode, K_O_RDONLY, 0));
        if (!file) {
            context.process->is64Bit = oldIs64Bit;
            testFail("/proc/cpuinfo open failed");
            return std::string();
        }
        U8 buffer[8192] = {};
        U32 length = file->readNative(buffer, sizeof(buffer));
        context.process->is64Bit = oldIs64Bit;
        return std::string((const char*)buffer, length);
    };
    std::string cpuInfo32 = readCpuInfo(false);
    std::string cpuInfo64 = readCpuInfo(true);
    const char* expected32[] = {
        "cpu family\t: 6\n",
        "model\t\t: 15\n",
        "stepping\t: 11\n",
        "cpuid level\t: 7\n",
        "flags\t\t: fpu tsc msr cx8 pge cmov clflush mmx fxsr sse sse2 pni ssse3 sse4_1 sse4_2 popcnt\n",
        "clflush size\t: 64\n",
        "address sizes\t: 32 bits physical, 32 bits virtual\n"
    };
    for (const char* line : expected32) {
        if (cpuInfo32.find(line) == std::string::npos) testFail("/proc/cpuinfo 32-bit missing line: %s", line);
    }
    const char* expected64[] = {
        "cpu family\t: 15\n",
        "model\t\t: 4\n",
        "model name\t: Intel(R) Pentium(R) 4 CPU 3.00GHz\n",
        "stepping\t: 1\n",
        "cpuid level\t: 1\n",
        "flags\t\t: fpu tsc msr cx8 cmov clflush mmx fxsr sse sse2 cx16 popcnt nx syscall lm lahf_lm 3dnowprefetch\n",
        "address sizes\t: 43 bits physical, 48 bits virtual\n"
    };
    for (const char* line : expected64) {
        if (cpuInfo64.find(line) == std::string::npos) testFail("/proc/cpuinfo 64-bit missing line: %s", line);
    }
    KSystem::procNode = oldProcNode;
    context.process->is64Bit = oldIs64Bit;
    testNewInstruction(0);
}
#endif
