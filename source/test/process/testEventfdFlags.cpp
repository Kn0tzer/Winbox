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
#include "kevent.h"

void testEventfdFlags() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    if ((S32)syscall_eventfd2(context.thread, 0, 0x40000000u) != -K_EINVAL) {
        testFail("eventfd2 accepted an unsupported flag");
    }
    S32 fd = (S32)syscall_eventfd2(context.thread, 7, K_O_NONBLOCK | K_O_CLOEXEC);
    KFileDescriptorPtr descriptor = fd >= 0 ? process->getFileDescriptor((U32)fd) : KFileDescriptorPtr();
    std::shared_ptr<KEvent> event = descriptor ? std::dynamic_pointer_cast<KEvent>(descriptor->kobject) : nullptr;
    if (!event || !(descriptor->accessFlags & K_O_NONBLOCK) || !(descriptor->descriptorFlags & FD_CLOEXEC) ||
        event->blocking || event->counter != 7) {
        testFail("eventfd2 did not preserve supported flags and initial counter");
    }
    if (fd >= 0) process->close((U32)fd);
}

#endif
