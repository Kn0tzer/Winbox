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
    // KProcess::commandLine holds it (kprocess.cpp joins argv with a
    // NUL delimiter). The segments are appended with explicit lengths
    // rather than built with BString::join so this guard stays
    // independent of join (join has its own guard, testBStringJoin).
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

// BString::join's BString overload must append the delimiter with its
// true length(). It used to delegate through the const char* overload,
// which measures the delimiter with strlen -- so the NUL delimiter
// KProcess::commandLine joins argv with contributed zero bytes and argv
// landed concatenated ('wine64C:\windows\system32\wineboot.exe--init'
// in exit logs). The const char* overload keeps C-string semantics by
// contract (matching append(const char*)); only the BString overload
// can express a NUL delimiter.
void testBStringJoin() {
    const char* exe = "C:\\windows\\system32\\wineboot.exe";
    int exeLen = (int)strlen(exe);
    std::vector<BString> args;
    args.push_back(BString("wine64", true));
    args.push_back(BString(exe, true));
    args.push_back(BString("--init", true));

    BString joined = BString::join(BString('\0'), args);
    if (joined.length() != 6 + 1 + exeLen + 1 + 6) {
        testFail("join(BString) must insert the full delimiter between values");
        return;
    }
    if (joined.charAt(6) != '\0' || joined.charAt(7 + exeLen) != '\0') {
        testFail("join(BString) NUL delimiter must land between the argv segments");
        return;
    }
    if (joined.substr(0, 6) != "wine64" || joined.substr(7, exeLen) != exe || joined.substr(8 + exeLen) != "--init") {
        testFail("join(BString) segments must survive byte-for-byte");
        return;
    }
    if (!joined.contains("wineboot.exe")) {
        testFail("join(BString) result must stay searchable past the NUL separators");
        return;
    }

    // The const char* overload keeps strlen semantics: "\0" is an
    // empty delimiter there. Pin the contract so the two overloads are
    // never conflated again.
    BString joinedCStr = BString::join("\0", args);
    BString concatenated("wine64", true);
    concatenated.append(exe);
    concatenated.append("--init");
    if (joinedCStr != concatenated) {
        testFail("join(const char*) must keep C-string (strlen) delimiter semantics");
        return;
    }

    // Ordinary delimiters behave the same through both overloads, a
    // single value gets no delimiter, and no values join to empty.
    std::vector<BString> abc;
    abc.push_back(BString("a", true));
    abc.push_back(BString("b", true));
    abc.push_back(BString("c", true));
    if (BString::join(", ", abc) != "a, b, c" || BString::join(BString(", ", true), abc) != "a, b, c") {
        testFail("join with an ordinary delimiter regressed");
        return;
    }
    std::vector<BString> one;
    one.push_back(BString("solo", true));
    if (BString::join(BString('\0'), one) != "solo") {
        testFail("join of a single value must not add a delimiter");
        return;
    }
    std::vector<BString> none;
    if (!BString::join(BString('\0'), none).isEmpty()) {
        testFail("join of no values must be empty");
        return;
    }
}

// BString::indexOf/lastIndexOf must search the full length() bytes,
// like contains: the strstr/strchr/strlen-view implementations stopped
// at the first embedded NUL, so nothing past it could be found.
void testBStringIndexOf() {
    BString s("ab", true);
    s.append("\0", 1);
    s.append("cd", 2);
    s.append("\0", 1);
    s.append("ef", 2);
    // s == "ab\0cd\0ef", length 8

    if (s.indexOf("cd") != 3 || s.indexOf("ef") != 6) {
        testFail("indexOf must find needles past an embedded NUL");
        return;
    }
    if (s.indexOf('e') != 6 || s.indexOf('c') != 3 || s.indexOf('x') != -1) {
        testFail("indexOf(char) must find characters past an embedded NUL");
        return;
    }
    if (s.indexOf("cd", 4) != -1 || s.indexOf("ef", 4) != 6 || s.indexOf("ab", 1) != -1) {
        testFail("indexOf must honor fromIndex");
        return;
    }
    if (s.indexOf("") != 0 || s.indexOf("", 2) != 2) {
        testFail("indexOf empty needle must match at fromIndex (strstr semantics)");
        return;
    }
    // A BString needle carrying an embedded NUL matches byte-for-byte
    // (the BString overload used to truncate it to its first segment).
    BString spanning("d", true);
    spanning.append("\0", 1);
    spanning.append("e", 1);
    if (s.indexOf(spanning) != 4) {
        testFail("indexOf(BString) must not truncate a needle at its embedded NUL");
        return;
    }
    if (s.lastIndexOf('f') != 7 || s.lastIndexOf('a') != 0 || s.lastIndexOf('x') != -1) {
        testFail("lastIndexOf(char) must find characters past an embedded NUL");
        return;
    }
    if (s.lastIndexOf('c', 2) != -1 || s.lastIndexOf('c', 3) != 3) {
        testFail("lastIndexOf(char) must honor fromIndex");
        return;
    }
    // lastIndexOf(const char*) keeps its find_last_of character-set
    // semantics: the last position holding any of the needle's chars.
    if (s.lastIndexOf("dc") != 4) {
        testFail("lastIndexOf(const char*) must keep character-set semantics");
        return;
    }

    // NUL-free behavior is unchanged, including the path idioms the
    // call sites rely on (Fs::getParentPath passes fromIndex).
    BString path("/a/b/c", true);
    if (path.lastIndexOf('/') != 4 || path.lastIndexOf('/', 3) != 2 || path.lastIndexOf('/', path.length() - 2) != 4) {
        testFail("lastIndexOf on a NUL-free path regressed");
        return;
    }
    BString plain("hello world", true);
    if (plain.indexOf("world") != 6 || plain.indexOf('o') != 4 || plain.lastIndexOf('o') != 7 || plain.indexOf("mars") != -1) {
        testFail("indexOf/lastIndexOf on a NUL-free string regressed");
        return;
    }
    BString empty;
    if (empty.indexOf("x") != -1 || empty.lastIndexOf('x') != -1) {
        testFail("empty string must have no matches");
        return;
    }
}

#endif
