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

#include <fstream>
#include <string>
#include <iterator>

#include "../cpu/testCPU.h"

// Keep a source-level guard around the x64 ABI dispatcher. The ordinary native
// test build does not enable BOXEDWINE_GUEST_X64, so it cannot call that
// dispatcher directly; deriving its path from __FILE__ lets this guard still
// inspect the exact code that will be compiled by the x64 target.
void testSyscall64DispatchGuard() {
    std::string testPath = __FILE__;
    const std::string marker = "/source/test/";
    const size_t markerAt = testPath.find(marker);
    if (markerAt == std::string::npos) {
        testFail("syscall64 dispatch guard cannot locate source tree from %s", testPath.c_str());
        return;
    }
    const std::string sourcePath = testPath.substr(0, markerAt) + "/source/kernel/syscall64.cpp";
    std::ifstream sourceFile(sourcePath);
    if (!sourceFile) {
        testFail("syscall64 dispatch guard cannot read %s", sourcePath.c_str());
        return;
    }
    const std::string source((std::istreambuf_iterator<char>(sourceFile)), std::istreambuf_iterator<char>());
    const size_t dispatchAt = source.find("void ksyscall64(");
    const size_t switchAt = source.find("switch (nr)", dispatchAt);
    if (dispatchAt == std::string::npos || switchAt == std::string::npos) {
        testFail("syscall64 dispatch guard cannot find dispatcher switch");
        return;
    }

    // Extract the switch body with brace depth so nested case scopes do not
    // confuse the assertions below.
    const size_t openAt = source.find('{', switchAt);
    size_t closeAt = openAt;
    int depth = 0;
    for (; closeAt < source.size(); ++closeAt) {
        if (source[closeAt] == '{') depth++;
        else if (source[closeAt] == '}' && --depth == 0) break;
    }
    if (openAt == std::string::npos || closeAt >= source.size()) {
        testFail("syscall64 dispatch guard cannot delimit dispatcher switch");
        return;
    }
    const std::string dispatch = source.substr(openAt, closeAt - openAt + 1);
    const std::string fallback = "default:\n            ret = unimplementedSyscall64(nr);\n            break;";
    if (dispatch.find(fallback) == std::string::npos) {
        testFail("x64 syscall default branch must use the central logged -ENOSYS path");
    }
    if (dispatch.find("ksyscall64: unimplemented syscall #%llu (%s)") != std::string::npos) {
        testFail("x64 syscall fallback logging must stay centralized");
    }
    const std::string helper = "static U64 unimplementedSyscall64(U64 nr)";
    const size_t helperAt = source.find(helper);
    const size_t helperEnd = source.find("#ifdef __EMSCRIPTEN__", helperAt);
    if (helperAt == std::string::npos || helperEnd == std::string::npos) {
        testFail("x64 syscall central ENOSYS helper is missing");
        return;
    }
    const std::string helperBody = source.substr(helperAt, helperEnd - helperAt);
    if (helperBody.find("reportedNumbers.insert((U32)nr).second") == std::string::npos ||
        helperBody.find("return (U64)-K_ENOSYS;") == std::string::npos ||
        helperBody.find("ksyscall64: unimplemented syscall #%llu (%s)") == std::string::npos) {
        testFail("x64 syscall fallback must log once per number and return -ENOSYS");
    }
}

#endif
