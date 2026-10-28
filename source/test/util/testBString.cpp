/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef __TEST

#include "../cpu/testCPU.h"

// BString::contains must search the full length() bytes of the string,
// not stop at the first NUL byte. KProcess::commandLine joins argv with
// NUL separators (kprocess.cpp) and the BW64_TRACE_EXE syscall tracer
// matches its substring against commandLine; with a strstr-based
// contains, nothing past argv[0] could ever match and the tracer
// silently never fired for the process it was built to watch (found by
// the session-teardown diagnosis lane, 2026-10-28).
void testBStringContains() {
    // Build an argv-style byte string with real NUL separators, the way
    // KProcess::commandLine is written to hold it (kprocess.cpp joins
    // argv with a NUL delimiter). BString::join cannot be used to build
    // it here: its const char* delimiter is measured with strlen, so a
    // NUL delimiter contributes zero bytes (a separate latent defect,
    // recorded in the session-teardown lane report, 2026-10-28). Append
    // the segments with explicit lengths instead.
    BString commandLine("/usr/lib/wine/wine64", true);
    commandLine.append("\0", 1);
    commandLine.append("snake.exe", 9);
    commandLine.append("\0", 1);
    commandLine.append("--windowed", 10);

    if (!commandLine.contains("wine64")) {
        testFail("contains must find a needle in argv[0]");
        return;
    }
    if (!commandLine.contains("snake.exe")) {
        testFail("contains must find a needle past the first embedded NUL (argv[1])");
        return;
    }
    if (!commandLine.contains("windowed")) {
        testFail("contains must find a needle in argv[2]");
        return;
    }
    if (!commandLine.contains("SNAKE.EXE", true)) {
        testFail("contains(ignoreCase) must find a needle past the first embedded NUL");
        return;
    }
    if (commandLine.contains("doom.exe")) {
        testFail("contains must not find an absent needle");
        return;
    }
    if (commandLine.contains("snake.exe ")) {
        testFail("contains must not match past the end of a segment");
        return;
    }

    // The BString overload takes the needle length from the needle
    // itself, so a needle containing an embedded NUL can match across a
    // segment boundary byte-for-byte.
    BString spanning("exe", true);
    spanning.append("\0", 1);
    spanning.append("--", 2);
    if (!commandLine.contains(spanning)) {
        testFail("contains(BString) must match a needle spanning an embedded NUL");
        return;
    }
    if (!commandLine.contains(BString("snake.exe", true))) {
        testFail("contains(BString) must find a needle past the first embedded NUL");
        return;
    }

    // NUL-free behavior is unchanged, including the legacy edge cases:
    // an empty needle matches a non-empty string, an empty string
    // contains nothing.
    BString plain("hello world", true);
    if (!plain.contains("world") || plain.contains("mars")) {
        testFail("contains on a NUL-free string regressed");
        return;
    }
    if (!plain.contains("")) {
        testFail("empty needle must match a non-empty string (strstr semantics)");
        return;
    }
    BString empty;
    if (empty.contains("x") || empty.contains("")) {
        testFail("empty string must contain nothing");
        return;
    }
}

#endif
