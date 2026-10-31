/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include "../../io/fs.h"
#include "../../io/fszip.h"
#include "../cpu/testCPU.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __TEST

// Fs::readNativeFile reports "nothing read" as 0. A host read error used to
// come back as 0xFFFFFFFF (the -1 return cast to U32), and a caller writes
// buffer[result] = 0 with that value. Reading a directory is the easiest
// deterministic host read error: open succeeds, read fails with EISDIR.
void testFsReadNativeFile() {
    char dirPath[256];
    snprintf(dirPath, sizeof(dirPath), "/tmp/bw64-fsread-dir-%d", (int)getpid());
    mkdir(dirPath, 0755);

    U8 buffer[16];
    memset(buffer, 0xAA, sizeof(buffer));
    U32 result = Fs::readNativeFile(B(dirPath), buffer, sizeof(buffer));
    if (result != 0) {
        testFail("Fs::readNativeFile(directory) returned %u, expected 0", result);
    }

    char missingPath[256];
    snprintf(missingPath, sizeof(missingPath), "/tmp/bw64-fsread-missing-%d", (int)getpid());
    unlink(missingPath);
    result = Fs::readNativeFile(B(missingPath), buffer, sizeof(buffer));
    if (result != 0) {
        testFail("Fs::readNativeFile(missing file) returned %u, expected 0", result);
    }

    char filePath[256];
    snprintf(filePath, sizeof(filePath), "/tmp/bw64-fsread-file-%d", (int)getpid());
    FILE* f = fopen(filePath, "wb");
    if (!f) {
        testFail("could not create temp file %s", filePath);
        rmdir(dirPath);
        return;
    }
    fwrite("abc", 1, 3, f);
    fclose(f);

    memset(buffer, 0, sizeof(buffer));
    result = Fs::readNativeFile(B(filePath), buffer, sizeof(buffer));
    if (result != 3 || memcmp(buffer, "abc", 3) != 0) {
        testFail("Fs::readNativeFile(small file) returned %u, expected 3 with contents", result);
    }
    result = Fs::readNativeFile(B(filePath), buffer, 2);
    if (result != 2) {
        testFail("Fs::readNativeFile(partial buffer) returned %u, expected 2", result);
    }

    unlink(filePath);
    rmdir(dirPath);
}

namespace {

U32 testCrc32(const U8* data, U32 len) {
    U32 crc = 0xFFFFFFFF;
    for (U32 i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

void put16(FILE* f, U32 value) {
    fputc(value & 0xFF, f);
    fputc((value >> 8) & 0xFF, f);
}

void put32(FILE* f, U32 value) {
    put16(f, value & 0xFFFF);
    put16(f, (value >> 16) & 0xFFFF);
}

// Writes a minimal single-entry stored (uncompressed) zip holding
// "a.txt" -> "hello", with the CRC field set to crcField.
bool writeStoredZip(const char* path, U32 crcField) {
    const char* name = "a.txt";
    const U8 data[] = { 'h', 'e', 'l', 'l', 'o' };
    const U32 nameLen = 5;
    const U32 dataLen = 5;
    const U32 cdOffset = 30 + nameLen + dataLen;
    const U32 cdSize = 46 + nameLen;

    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    // local file header
    put32(f, 0x04034b50);
    put16(f, 20);        // version needed
    put16(f, 0);         // flags
    put16(f, 0);         // method: stored
    put16(f, 0);         // mod time
    put16(f, 0x21);      // mod date
    put32(f, crcField);
    put32(f, dataLen);   // compressed size
    put32(f, dataLen);   // uncompressed size
    put16(f, nameLen);
    put16(f, 0);         // extra length
    fwrite(name, 1, nameLen, f);
    fwrite(data, 1, dataLen, f);
    // central directory header
    put32(f, 0x02014b50);
    put16(f, 20);        // version made by
    put16(f, 20);        // version needed
    put16(f, 0);         // flags
    put16(f, 0);         // method
    put16(f, 0);         // mod time
    put16(f, 0x21);      // mod date
    put32(f, crcField);
    put32(f, dataLen);
    put32(f, dataLen);
    put16(f, nameLen);
    put16(f, 0);         // extra length
    put16(f, 0);         // comment length
    put16(f, 0);         // disk number start
    put16(f, 0);         // internal attributes
    put32(f, 0);         // external attributes
    put32(f, 0);         // local header offset
    fwrite(name, 1, nameLen, f);
    // end of central directory
    put32(f, 0x06054b50);
    put16(f, 0);
    put16(f, 0);
    put16(f, 1);
    put16(f, 1);
    put32(f, cdSize);
    put32(f, cdOffset);
    put16(f, 0);
    fclose(f);
    return true;
}

} // namespace

// A zip entry whose stored CRC does not match its contents is corrupt.
// The vendored minizip only reports that from unzCloseCurrentFile, whose
// result FsZip::readFileFromZip used to discard, so the corrupted entry
// was handed to the caller as good data.
void testFsZipReadBadCrc() {
#ifdef BOXEDWINE_ZLIB
    const U8 data[] = { 'h', 'e', 'l', 'l', 'o' };
    const U32 goodCrc = testCrc32(data, 5);

    char goodPath[256];
    snprintf(goodPath, sizeof(goodPath), "/tmp/bw64-fszip-good-%d.zip", (int)getpid());
    if (!writeStoredZip(goodPath, goodCrc)) {
        testFail("could not write good test zip %s", goodPath);
        return;
    }
    BString result;
    if (!FsZip::readFileFromZip(B(goodPath), B("a.txt"), result)) {
        testFail("FsZip::readFileFromZip refused a well-formed entry");
    } else if (result != B("hello")) {
        testFail("FsZip::readFileFromZip returned wrong contents: %s", result.c_str());
    }
    unlink(goodPath);

    char badPath[256];
    snprintf(badPath, sizeof(badPath), "/tmp/bw64-fszip-bad-%d.zip", (int)getpid());
    if (!writeStoredZip(badPath, goodCrc ^ 0xFFFFFFFF)) {
        testFail("could not write corrupt test zip %s", badPath);
        return;
    }
    result = B("");
    if (FsZip::readFileFromZip(B(badPath), B("a.txt"), result)) {
        testFail("FsZip::readFileFromZip accepted an entry with a bad CRC");
    }
    unlink(badPath);
#endif
}

#endif
