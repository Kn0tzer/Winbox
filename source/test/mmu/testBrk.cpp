/*
 * Test 32-bit brk/mmap heap emulation (glibc malloc simulation)
 */
#include "boxedwine.h"

#ifdef __TEST

#include "../cpu/testCPU.h"
#include "../../emulation/softmmu/kmemory_soft.h"

void testDemandPageIsolation() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;
    constexpr U32 pageCount = 8;
    U32 base = memory->mmap(context.thread, 0, pageCount * K_PAGE_SIZE,
        K_PROT_READ | K_PROT_WRITE, K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    if ((S32)base < 0) {
        testFail("anonymous demand-page mapping failed: %08X", base);
        return;
    }

    // Direct access to unbacked pages would dereference host address zero.
    // Check this invariant before exercising the accesses, so a regression
    // reports a test failure instead of corrupting the host or crashing it.
    for (U32 i = 0; i < pageCount; i++) {
        MMU& page = getMemData(memory)->mmu[(base >> K_PAGE_SHIFT) + i];
        if (!page.ramIndex && (page.canReadRam || page.canWriteRam)) {
            testFail("unbacked anonymous page %u permits direct RAM access", i);
            memory->unmap(base, pageCount * K_PAGE_SIZE);
            return;
        }
    }

    // First touch through each direct-access width must allocate a private,
    // zero-filled page. Touch alternating pages with writes before reading
    // the others to detect aliasing of pages with the same offset.
    memory->writeb(base + 17, 0x12);
    memory->writew(base + 2 * K_PAGE_SIZE + 17, 0x3456);
    memory->writed(base + 4 * K_PAGE_SIZE + 17, 0x789ABCDE);
    memory->writedInline(base + 6 * K_PAGE_SIZE + 17, 0xFEDCBA98);
    for (U32 i = 1; i < pageCount; i += 2) {
        U32 address = base + i * K_PAGE_SIZE + 17;
        U32 value = i == 1 ? memory->readb(address) :
            i == 3 ? memory->readw(address) :
            i == 5 ? memory->readd(address) : memory->readdInline(address);
        if (value != 0) testFail("fresh anonymous page %u is not zero-filled: %08X", i, value);
    }
    if (memory->readb(base + 17) != 0x12 ||
        memory->readw(base + 2 * K_PAGE_SIZE + 17) != 0x3456 ||
        memory->readd(base + 4 * K_PAGE_SIZE + 17) != 0x789ABCDE ||
        memory->readdInline(base + 6 * K_PAGE_SIZE + 17) != 0xFEDCBA98) {
        testFail("anonymous pages alias or lose their first writes");
    }
    memory->unmap(base, pageCount * K_PAGE_SIZE);
}

void testBrkHeap() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KMemory* memory = context.memory;
    KThread* thread = context.thread;

    // Simulate a loaded binary: brkEnd at end of binary (like loader.cpp sets)
    // Binary loaded at 0xF0000, size 0x10000 -> brkEnd = 0x100000
    U32 fakeBinaryEnd = 0x100000;
    process->brkEnd = fakeBinaryEnd;

    // glibc calls brk(0) to get current brk
    U32 brk0 = process->brk(thread, 0);
    if (brk0 != fakeBinaryEnd) {
        testFail("brk(0) expected 0x%X, got 0x%X", fakeBinaryEnd, brk0);
        return;
    }

    // glibc grows heap: brk(new_end) - simulate malloc needing more
    U32 newEnd = fakeBinaryEnd + 0x1000;
    U32 result = process->brk(thread, newEnd);
    if (result != newEnd) {
        testFail("brk grow to 0x%X failed, got 0x%X", newEnd, result);
        return;
    }

    // Verify the new heap memory is writable and readable
    for (U32 addr = fakeBinaryEnd; addr < newEnd; addr += 4) {
        memory->writed(addr, 0xDEADBEEF);
    }
    for (U32 addr = fakeBinaryEnd; addr < newEnd; addr += 4) {
        U32 val = memory->readd(addr);
        if (val != 0xDEADBEEF) {
            testFail("heap memory corruption at 0x%X", addr);
            return;
        }
    }

    // Grow more: simulate multiple mallocs (glibc grows in chunks)
    for (int i = 0; i < 10; i++) {
        U32 oldEnd = process->brkEnd;
        U32 growTo = oldEnd + 0x2000;
        U32 r = process->brk(thread, growTo);
        if (r != growTo) {
            testFail("brk grow iter %d to 0x%X failed, got 0x%X", i, growTo, r);
            return;
        }
        for (U32 addr = oldEnd; addr < growTo; addr += 4) {
            memory->writed(addr, 0x1000 + (U32)i);
        }
    }

    // Verify all patterns intact (no overlap/corruption)
    for (int i = 0; i < 10; i++) {
        U32 regionStart = fakeBinaryEnd + 0x1000 + ((U32)i * 0x2000);
        U32 regionEnd = regionStart + 0x2000;
        for (U32 addr = regionStart; addr < regionEnd; addr += 4096) {
            U32 val = memory->readd(addr);
            if (val != (U32)(0x1000 + i)) {
                testFail("heap region %d corruption at 0x%X", i, addr);
                return;
            }
        }
    }

    // Test large mmap (glibc uses mmap for >128KB)
    U32 mmapAddr = memory->mmap(thread, 0, 256*1024,
                                K_PROT_READ | K_PROT_WRITE,
                                K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    if ((S32)mmapAddr < 0) {
        testFail("mmap 256KB failed: 0x%X", mmapAddr);
        return;
    }
    for (U32 addr = mmapAddr; addr < mmapAddr + 256*1024; addr += 4096) {
        memory->writed(addr, 0xCAFEBABE);
    }
    for (U32 addr = mmapAddr; addr < mmapAddr + 256*1024; addr += 4096) {
        U32 val = memory->readd(addr);
        if (val != 0xCAFEBABE) {
            testFail("mmap region corruption at 0x%X", addr);
            return;
        }
    }

    // brk should still work after mmap (regions must not overlap)
    U32 heapEnd = process->brkEnd;
    U32 growTo = heapEnd + 0x5000;
    U32 r = process->brk(thread, growTo);
    if (r != growTo) {
        testFail("brk after mmap failed: expected 0x%X got 0x%X", growTo, r);
        return;
    }
    U32 val = memory->readd(mmapAddr);
    if (val != 0xCAFEBABE) {
        testFail("brk overwrote mmap region!");
        return;
    }

    // Test munmap (glibc uses for large frees)
    U32 mmap2 = memory->mmap(thread, 0, 64*1024,
                             K_PROT_READ | K_PROT_WRITE,
                             K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    if ((S32)mmap2 < 0) {
        testFail("mmap2 64KB failed");
        return;
    }
    memory->writed(mmap2, 0x12345678);
    U32 unmapResult = memory->unmap(mmap2, 64*1024);
    if (unmapResult != 0) {
        testFail("munmap failed: 0x%X", unmapResult);
        return;
    }
    // Re-mmap should reuse the freed region (or at least not overlap heap)
    U32 mmap3 = memory->mmap(thread, 0, 64*1024,
                             K_PROT_READ | K_PROT_WRITE,
                             K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    if ((S32)mmap3 < 0) {
        testFail("mmap3 after munmap failed");
        return;
    }
    // Verify heap still intact after munmap/mmap cycle
    U32 heapVal = memory->readd(fakeBinaryEnd);
    if (heapVal != 0xDEADBEEF) {
        testFail("heap corrupted after munmap/mmap cycle!");
        return;
    }
}
#endif
