/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"

#ifdef __TEST

#include "../cpu/testCPU.h"
#include "../../io/fsfilenode.h"
#include "../../io/fsmemopennode.h"

void testMemfdSeals() {
    KProcessPtr process = testContext().process;
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    if (process->memfd_create(B("bad-flags"), 0x80000000u) != (U32)-K_EINVAL) {
        testFail("memfd_create accepted unknown flag bits");
    }
    S32 defaultFd = (S32)process->memfd_create(B("default-seals"), 0);
    if (defaultFd < 0 || process->fcntrl(testContext().thread, (U32)defaultFd, K_F_GET_SEALS, 0) != K_F_SEAL_SEAL ||
        process->fcntrl(testContext().thread, (U32)defaultFd, K_F_ADD_SEALS, K_F_SEAL_GROW) != (U32)-K_EPERM) {
        testFail("default memfd did not start with immutable F_SEAL_SEAL");
    }
    if (defaultFd >= 0) process->close((U32)defaultFd);
    S32 fd = (S32)process->memfd_create(B("seal-regression"), 2 /*MFD_ALLOW_SEALING*/);
    if (fd < 0) {
        testFail("memfd_create(MFD_ALLOW_SEALING) returned %d", fd);
        return;
    }
    KFileDescriptorPtr descriptor = process->getFileDescriptor((U32)fd);
    std::shared_ptr<KFile> file = descriptor ? std::dynamic_pointer_cast<KFile>(descriptor->kobject) : nullptr;
    FsMemOpenNode* node = file ? dynamic_cast<FsMemOpenNode*>(file->openFile) : nullptr;
    if (!node || process->fcntrl(testContext().thread, (U32)fd, K_F_GET_SEALS, 0) != 0 ||
        process->ftruncate64((U32)fd, 16) != 0) {
        testFail("new sealable memfd did not expose an empty seal set or allow initial sizing");
        process->close((U32)fd);
        return;
    }
    const U32 seals = K_F_SEAL_SHRINK | K_F_SEAL_GROW | K_F_SEAL_WRIT;
    if (process->fcntrl(testContext().thread, (U32)fd, K_F_ADD_SEALS, seals) != 0 ||
        process->fcntrl(testContext().thread, (U32)fd, K_F_GET_SEALS, 0) != seals) {
        testFail("F_ADD_SEALS/F_GET_SEALS did not persist SHRINK/GROW/WRITE seals");
    }
    if (process->ftruncate64((U32)fd, 8) != (U32)-K_EPERM ||
        process->ftruncate64((U32)fd, 32) != (U32)-K_EPERM) {
        testFail("sealed memfd allowed shrink or growth");
    }
    U8 byte = 0x5a;
    if ((S32)node->writeNative(&byte, 1) != -K_EPERM) {
        testFail("F_SEAL_WRITE did not reject a write");
    }
    if (process->fcntrl(testContext().thread, (U32)fd, K_F_ADD_SEALS, 0x80000000u) != (U32)-K_EINVAL) {
        testFail("F_ADD_SEALS accepted unknown seal bits");
    }
    if (process->fcntrl(testContext().thread, (U32)fd, K_F_ADD_SEALS, K_F_SEAL_SEAL) != 0 ||
        process->fcntrl(testContext().thread, (U32)fd, K_F_ADD_SEALS, K_F_SEAL_GROW) != (U32)-K_EPERM) {
        testFail("F_SEAL_SEAL did not prevent subsequent seal changes");
    }
    process->close((U32)fd);
}

#endif
