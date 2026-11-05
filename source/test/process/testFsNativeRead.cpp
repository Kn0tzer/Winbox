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
#include "../../io/fsfilenode.h"
#include "../../io/fsfileopennode.h"
#include <fcntl.h>
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
bool writeZipWithEntry(const char* path, U32 crcField, U16 method, const U8* payload, U32 payloadLen, U32 declaredUncompSize);

bool writeStoredZip(const char* path, U32 crcField) {
    return writeZipWithEntry(path, crcField, 0, (const U8*)"hello", 5, 5);
}

// Writes a minimal single-entry zip holding "a.txt", with the entry
// payload, compression method and declared uncompressed size given.
bool writeZipWithEntry(const char* path, U32 crcField, U16 method, const U8* payload, U32 payloadLen, U32 declaredUncompSize) {
    const char* name = "a.txt";
    const U32 nameLen = 5;
    const U32 dataLen = payloadLen;
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
    put16(f, method);         // method
    put16(f, 0);         // mod time
    put16(f, 0x21);      // mod date
    put32(f, crcField);
    put32(f, dataLen);   // compressed size
    put32(f, declaredUncompSize);   // uncompressed size
    put16(f, nameLen);
    put16(f, 0);         // extra length
    fwrite(name, 1, nameLen, f);
    fwrite(payload, 1, dataLen, f);
    // central directory header
    put32(f, 0x02014b50);
    put16(f, 20);        // version made by
    put16(f, 20);        // version needed
    put16(f, 0);         // flags
    put16(f, method);         // method
    put16(f, 0);         // mod time
    put16(f, 0x21);      // mod date
    put32(f, crcField);
    put32(f, dataLen);
    put32(f, declaredUncompSize);
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

namespace {

struct DeclTestEntry {
    const char* name;
    const char* payload;
    bool badCrc;
};

// Writes a stored (uncompressed) multi-entry zip with correct local and
// central headers; entries flagged badCrc get a deliberately wrong CRC.
bool writeMultiStoredZip(const char* path, const DeclTestEntry* entries, U32 count) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    U32 offsets[16];
    for (U32 i = 0; i < count; i++) {
        const U32 nameLen = (U32)strlen(entries[i].name);
        const U32 dataLen = (U32)strlen(entries[i].payload);
        U32 crc = testCrc32((const U8*)entries[i].payload, dataLen);
        if (entries[i].badCrc) {
            crc ^= 0xFFFFFFFF;
        }
        offsets[i] = (U32)ftell(f);
        put32(f, 0x04034b50);
        put16(f, 20);        // version needed
        put16(f, 0);         // flags
        put16(f, 0);         // method: stored
        put16(f, 0);         // mod time
        put16(f, 0x21);      // mod date
        put32(f, crc);
        put32(f, dataLen);   // compressed size
        put32(f, dataLen);   // uncompressed size
        put16(f, nameLen);
        put16(f, 0);         // extra length
        fwrite(entries[i].name, 1, nameLen, f);
        fwrite(entries[i].payload, 1, dataLen, f);
    }
    const U32 cdOffset = (U32)ftell(f);
    for (U32 i = 0; i < count; i++) {
        const U32 nameLen = (U32)strlen(entries[i].name);
        const U32 dataLen = (U32)strlen(entries[i].payload);
        U32 crc = testCrc32((const U8*)entries[i].payload, dataLen);
        if (entries[i].badCrc) {
            crc ^= 0xFFFFFFFF;
        }
        put32(f, 0x02014b50);
        put16(f, 20);        // version made by
        put16(f, 20);        // version needed
        put16(f, 0);         // flags
        put16(f, 0);         // method: stored
        put16(f, 0);         // mod time
        put16(f, 0x21);      // mod date
        put32(f, crc);
        put32(f, dataLen);
        put32(f, dataLen);
        put16(f, nameLen);
        put16(f, 0);         // extra length
        put16(f, 0);         // comment length
        put16(f, 0);         // disk number start
        put16(f, 0);         // internal attributes
        put32(f, 0);         // external attributes
        put32(f, offsets[i]);
        fwrite(entries[i].name, 1, nameLen, f);
    }
    const U32 cdSize = (U32)ftell(f) - cdOffset;
    put32(f, 0x06054b50);
    put16(f, 0);
    put16(f, 0);
    put16(f, count);
    put16(f, count);
    put32(f, cdSize);
    put32(f, cdOffset);
    put16(f, 0);
    fclose(f);
    return true;
}

} // namespace

// FsZip::init captures the root-level fullfills.txt / depends.txt
// declarations during the central-directory walk it already performs,
// so StartupArgs no longer scans every archive twice before init. The
// capture must agree with FsZip::readFileFromZip on the same archive:
// the first exact root-level name match wins, subdirectory namesakes do
// not count, and a corrupt entry counts as absent.
void testFsZipDeclCapture() {
#ifdef BOXEDWINE_ZLIB
    char rootPath[256];
    snprintf(rootPath, sizeof(rootPath), "/tmp/bw64-fsdecl-root-%d", (int)getpid());
    Fs::initFileSystem(B(rootPath));

    char zipPath[256];
    snprintf(zipPath, sizeof(zipPath), "/tmp/bw64-fsdecl-%d.zip", (int)getpid());
    const DeclTestEntry entries[] = {
        { "sub/fullfills.txt", "decoy", false },
        { "depends.txt", "dep.zip", false },
        { "fullfills.txt", "provider", false },
        { "depends.txt", "second.zip", false },
        { "a.txt", "hello", false },
    };
    if (!writeMultiStoredZip(zipPath, entries, 5)) {
        testFail("could not write declaration test zip %s", zipPath);
        Fs::shutDown();
        rmdir(rootPath);
        return;
    }
    BString viaScan;
    if (!FsZip::readFileFromZip(B(zipPath), B("fullfills.txt"), viaScan) || viaScan != B("provider")) {
        testFail("readFileFromZip fullfills.txt = %s, expected provider", viaScan.c_str());
    }
    if (!FsZip::readFileFromZip(B(zipPath), B("depends.txt"), viaScan) || viaScan != B("dep.zip")) {
        testFail("readFileFromZip depends.txt = %s, expected dep.zip", viaScan.c_str());
    }
    {
        std::shared_ptr<FsZip> zip = std::make_shared<FsZip>();
        if (!zip->init(B(zipPath), B(""))) {
            testFail("FsZip::init failed on declaration test zip");
        } else {
            if (zip->fullfillsContent != B("provider")) {
                testFail("captured fullfills = %s, expected provider", zip->fullfillsContent.c_str());
            }
            if (zip->dependsContent != B("dep.zip")) {
                testFail("captured depends = %s, expected dep.zip", zip->dependsContent.c_str());
            }
        }
    }
    unlink(zipPath);

    // No declarations: both captures stay empty.
    char plainPath[256];
    snprintf(plainPath, sizeof(plainPath), "/tmp/bw64-fsdecl-plain-%d.zip", (int)getpid());
    const DeclTestEntry plain[] = { { "a.txt", "hello", false } };
    if (!writeMultiStoredZip(plainPath, plain, 1)) {
        testFail("could not write plain test zip %s", plainPath);
    } else {
        std::shared_ptr<FsZip> zip = std::make_shared<FsZip>();
        if (!zip->init(B(plainPath), B(""))) {
            testFail("FsZip::init failed on plain test zip");
        } else if (zip->fullfillsContent.length() || zip->dependsContent.length()) {
            testFail("plain zip captured declarations: %s / %s", zip->fullfillsContent.c_str(), zip->dependsContent.c_str());
        }
        unlink(plainPath);
    }

    // Corrupt declaration entry: treated as absent by both readers.
    char badPath[256];
    snprintf(badPath, sizeof(badPath), "/tmp/bw64-fsdecl-bad-%d.zip", (int)getpid());
    const DeclTestEntry bad[] = { { "fullfills.txt", "provider", true } };
    if (!writeMultiStoredZip(badPath, bad, 1)) {
        testFail("could not write corrupt declaration test zip %s", badPath);
    } else {
        BString result = B("sentinel");
        if (FsZip::readFileFromZip(B(badPath), B("fullfills.txt"), result)) {
            testFail("readFileFromZip accepted a corrupt fullfills.txt");
        }
        std::shared_ptr<FsZip> zip = std::make_shared<FsZip>();
        if (!zip->init(B(badPath), B(""))) {
            testFail("FsZip::init failed on corrupt declaration test zip");
        } else if (zip->fullfillsContent.length()) {
            testFail("corrupt fullfills.txt captured as %s", zip->fullfillsContent.c_str());
        }
        unlink(badPath);
    }

    Fs::shutDown();
    rmdir(rootPath);
#endif
}

// An entry whose deflate stream is garbage makes unzReadCurrentFile
// return a negative error code. The extraction loops used to store that
// in a U32 and hand it to fwrite as a length; afterwards a partial file
// could be left behind and unzip still reported success.
void testFsZipExtractCorrupt() {
#ifdef BOXEDWINE_ZLIB
    // The test harness never runs Fs::init(); production sets the native
    // path separator there. Without it, path splitting cannot work.
    if (Fs::nativePathSeperator.isEmpty()) {
        Fs::nativePathSeperator = B("/");
    }
    const U8 garbage[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    char zipPath[256];
    snprintf(zipPath, sizeof(zipPath), "/tmp/bw64-fszip-corrupt-%d.zip", (int)getpid());
    if (!writeZipWithEntry(zipPath, 0, 8, garbage, sizeof(garbage), 100)) {
        testFail("could not write corrupt-deflate test zip %s", zipPath);
        return;
    }

    // extractFileFromZip's third argument is the destination directory.
    char extractDir[256];
    snprintf(extractDir, sizeof(extractDir), "/tmp/bw64-fszip-corrupt-xdir-%d", (int)getpid());
    mkdir(extractDir, 0755);
    if (FsZip::extractFileFromZip(B(zipPath), B("a.txt"), B(extractDir))) {
        testFail("FsZip::extractFileFromZip accepted a corrupt deflate entry");
    }
    struct stat st;
    char extractedPath[512];
    snprintf(extractedPath, sizeof(extractedPath), "%s/a.txt", extractDir);
    if (stat(extractedPath, &st) == 0) {
        testFail("FsZip::extractFileFromZip left a partial file behind");
        unlink(extractedPath);
    }
    rmdir(extractDir);

    char outDir[256];
    snprintf(outDir, sizeof(outDir), "/tmp/bw64-fszip-corrupt-dir-%d", (int)getpid());
    mkdir(outDir, 0755);
    BString error = FsZip::unzip(B(zipPath), B(outDir), [](U32, BString) {});
    if (error.length() == 0) {
        testFail("FsZip::unzip reported success for a corrupt deflate entry");
    }
    char extracted[512];
    snprintf(extracted, sizeof(extracted), "%s/a.txt", outDir);
    if (stat(extracted, &st) == 0) {
        testFail("FsZip::unzip left a partial file behind");
        unlink(extracted);
    }
    rmdir(outDir);
    unlink(zipPath);
#endif
}

// Host read/write failures on an open file node used to come back as a
// 0xFFFFFFFF byte count (the -1 return cast to U32), which callers
// trusted as a huge successful transfer. An invalid handle is the
// deterministic host error: every read/write on it fails with EBADF.
void testFsFileOpenNodeIoErrors() {
    std::shared_ptr<FsFileNode> node = std::make_shared<FsFileNode>(0x3FFFFF01, 0, B("/host-io-test"), B(""), B(""), false, false, nullptr);
    {
        FsFileOpenNode bad(node, K_O_RDWR, 0xFFFFFFFF);
        U8 buffer[8] = {};
        if (bad.readNative(buffer, sizeof(buffer)) != 0) {
            testFail("FsFileOpenNode::readNative on an invalid handle did not return 0");
        }
        if (bad.writeNative(buffer, sizeof(buffer)) != 0) {
            testFail("FsFileOpenNode::writeNative on an invalid handle did not return 0");
        }
    }
    // A real descriptor still reads and writes exactly.
    char filePath[256];
    snprintf(filePath, sizeof(filePath), "/tmp/bw64-hostio-file-%d", (int)getpid());
    int f = ::open(filePath, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (f < 0) {
        testFail("could not create temp file %s", filePath);
        return;
    }
    {
        FsFileOpenNode good(node, K_O_RDWR, (U32)f);
        U8 out[3] = { 'x', 'y', 'z' };
        if (good.writeNative(out, 3) != 3) {
            testFail("FsFileOpenNode::writeNative on a real file did not write 3 bytes");
        }
        good.seek(0);
        U8 in[3] = {};
        if (good.readNative(in, 3) != 3 || memcmp(in, "xyz", 3) != 0) {
            testFail("FsFileOpenNode::readNative on a real file did not return the written bytes");
        }
    }
    unlink(filePath);
}

#endif
