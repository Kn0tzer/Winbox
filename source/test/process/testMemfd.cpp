/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include "../../io/fsfilenode.h"

#ifdef __TEST

#include "../cpu/testCPU.h"

// 64-bit guest support for memfd_create lives in syscall64.cpp (syscall 319),
// which needs a CPU64/KMemory64 and is not part of this 32-bit test harness.
// Exercise the underlying KProcess/file semantics it delegates to: flags are
// accepted, ftruncate sizes the memfd, mmap accepts MAP_SHARED, and sequential
// read/write operations preserve the file contents.
void testMemfdCreate() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KThread* thread = context.thread;
    KMemory* memory = context.memory;

    // The CPU test harness does not initialize the /proc tree, but descriptor
    // allocation publishes each fd under /proc/<pid>/fd. Give this process a
    // minimal parent node so the normal KProcess::memfd_create path is usable.
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }

    constexpr U32 kMfdCloexec = 1;
    constexpr U32 kMfdAllowSealing = 2;
    constexpr U32 bufferAddress = TEST_HEAP_ADDRESS;
    constexpr U32 memfdLength = 64;
    U32 firstInodeId = 0;

    const U32 flagSets[] = {0, kMfdCloexec, kMfdAllowSealing, kMfdCloexec | kMfdAllowSealing};
    const char* flagNames[] = {"flags=0", "MFD_CLOEXEC", "MFD_ALLOW_SEALING", "CLOEXEC|ALLOW_SEALING"};
    for (size_t i = 0; i < sizeof(flagSets) / sizeof(flagSets[0]); i++) {
        U32 fd = process->memfd_create(B("test-memfd"), flagSets[i]);
        if ((S32)fd < 0) {
            testFail("memfd_create(%s) failed: %d", flagNames[i], (S32)fd);
            return;
        }
        KFileDescriptorPtr descriptor = process->getFileDescriptor(fd);
        if (!descriptor || descriptor->kobject->type != KTYPE_FILE) {
            testFail("memfd_create(%s) did not return a file descriptor", flagNames[i]);
            process->close(fd);
            continue;
        }
        std::shared_ptr<KFile> file = std::dynamic_pointer_cast<KFile>(descriptor->kobject);
        U32 inodeId = file->openFile->node->id;
        if (i == 0) {
            firstInodeId = inodeId;
        } else if (inodeId == firstInodeId) {
            testFail("memfd_create(%s) reused inode id %u", flagNames[i], inodeId);
        }
        if (((descriptor->descriptorFlags & FD_CLOEXEC) != 0) != ((flagSets[i] & kMfdCloexec) != 0)) {
            testFail("memfd_create(%s) CLOEXEC mismatch: 0x%X", flagNames[i], descriptor->descriptorFlags);
        }
        if (!descriptor->canRead() || !descriptor->canWrite()) {
            testFail("memfd_create(%s) fd is not read/write", flagNames[i]);
        }
        if (!descriptor->kobject->isReadReady() || !descriptor->kobject->isWriteReady()) {
            testFail("memfd_create(%s) file is not read/write ready", flagNames[i]);
        }
        if ((S32)process->ftruncate64(fd, 0) < 0) {
            testFail("memfd_create(%s) ftruncate(0) failed", flagNames[i]);
        } else if (descriptor->kobject->length() != 0) {
            testFail("memfd_create(%s) ftruncate(0) left length %lld", flagNames[i],
                     (long long)descriptor->kobject->length());
        }
        process->close(fd);
    }

    U32 fd = process->memfd_create(B("test-memfd-rw"), kMfdCloexec);
    if ((S32)fd < 0) {
        testFail("memfd_create for read/write failed: %d", (S32)fd);
        return;
    }

    // Writes to an initially empty memfd append and must advance its offset.
    // A second write catches a stale file position that would overwrite the
    // first write instead of extending the file.
    for (U32 i = 0; i < 32; i++) {
        memory->writeb(bufferAddress + i, (U8)(0xA0 + i));
    }
    U32 written = process->write(thread, fd, bufferAddress, 16);
    if ((S32)written < 0 || written != 16) {
        testFail("memfd first write(16) reported %d", (S32)written);
        process->close(fd);
        return;
    }
    written = process->write(thread, fd, bufferAddress + 16, 16);
    if ((S32)written < 0 || written != 16) {
        testFail("memfd second write(16) reported %d", (S32)written);
        process->close(fd);
        return;
    }
    process->llseek(fd, 0, 0); // SEEK_SET
    U32 read = process->read(thread, fd, bufferAddress + 32, 32);
    if ((S32)read < 0 || read != 32) {
        testFail("memfd read(32) reported %d", (S32)read);
        process->close(fd);
        return;
    }
    for (U32 i = 0; i < 32; i++) {
        U8 value = memory->readb(bufferAddress + 32 + i);
        if (value != (U8)(0xA0 + i)) {
            testFail("memfd sequential byte %u: expected 0x%X got 0x%X", i, 0xA0 + i, value);
            process->close(fd);
            return;
        }
    }

    if ((S32)process->ftruncate64(fd, memfdLength) < 0) {
        testFail("memfd ftruncate(%u) failed", memfdLength);
        process->close(fd);
        return;
    }

    KFileDescriptorPtr descriptor = process->getFileDescriptor(fd);
    if (!descriptor) {
        testFail("memfd descriptor disappeared before mmap");
        process->close(fd);
        return;
    }
    if (descriptor->kobject->length() != memfdLength) {
        testFail("memfd ftruncate(%u) set length %lld", memfdLength,
                 (long long)descriptor->kobject->length());
        process->close(fd);
        return;
    }
    if (!descriptor->kobject->canMap()) {
        testFail("memfd file descriptor does not support mmap");
        process->close(fd);
        return;
    }

    U32 mappedAddress = memory->mmap(thread, 0, K_PAGE_SIZE, K_PROT_READ | K_PROT_WRITE,
                                     K_MAP_SHARED, fd, 0);
    if ((S32)mappedAddress < 0) {
        testFail("memfd mmap(MAP_SHARED) failed: %d", (S32)mappedAddress);
        process->close(fd);
        return;
    }
    if (memory->readb(mappedAddress) != 0xA0) {
        testFail("memfd MAP_SHARED mapping did not expose the file contents");
        memory->unmap(mappedAddress, K_PAGE_SIZE);
        process->close(fd);
        return;
    }
    memory->unmap(mappedAddress, K_PAGE_SIZE);
    process->close(fd);
}

#endif
