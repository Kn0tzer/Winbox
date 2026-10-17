/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
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

#ifdef BOXEDWINE_ZLIB

#ifndef __FSZIP_H__
#define __FSZIP_H__

#include "platformBoxedwine.h"

#include <atomic>

#undef OF
#define STRICTUNZIP
extern "C"
{
#include "../../lib/zlib/contrib/minizip/unzip.h"
}
#undef OF

class fsZipInfo {
public:
    fsZipInfo() = default;
    BString filename;
    BString link;
    bool isLink = false;
    bool isDirectory = false;
    U64 length = 0;
    U64 lastModified = 0;
    U64 offset = 0;
};

class FsZip : public std::enable_shared_from_this<FsZip> {
public:
    FsZip() = default;
    ~FsZip();
    bool init(BString zipPath, BString mount);
    unzFile zipfile = nullptr;

    // Path (or bw64url: spec) of the backing zip, stored so each FsZipOpenNode
    // can open its own independent decompression stream via openEntryStream().
    // (The old design shared the single `zipfile` stream above across all open
    // nodes behind one readMutex; the loader's seek-heavy small-read pattern
    // then re-decompressed every file 3-5x: each backward seek forced a
    // close -> reopen -> decompress-from-start-and-discard, and any interleave
    // between two open nodes forced the same. Now each node owns a private
    // stream plus a decompressed prefix cache, so each byte is decompressed at
    // most once per node and backward seeks are memcpy.)
    BString zipPath;

    // Set by init() when the zip layout indicates an x86_64-linux/wine64
    // rootfs: presence of any of `x86_64-linux-gnu/`, `wine/x86_64-unix/`,
    // `wine/x86_64-windows/`, or `/lib64/` in any entry path. Consumed by
    // KProcess::is64Bit selection at exec time (wiring lives in the
    // launcher; see ui/data/boxedContainer.cpp and the loader dispatch
    // in source/kernel/loader/loader.cpp). v1 detection only; the actual
    // exec routing landed alongside Milestone D rootfs work.
    bool guestIs64 = false;

    // Opens a fresh, independent decompression stream positioned at the start
    // of the entry at central-directory offset zipOffset. The caller owns the
    // handle (unzCloseCurrentFile + unzClose when done). Returns nullptr on
    // failure. Cheap: for bw64url: zips the compressed-byte block cache is
    // shared per spec across handles; for local zips it is one small
    // end-of-central-directory read per open.
    unzFile openEntryStream(U64 zipOffset);

    // Decompression-efficiency counters for the per-node stream + prefix-cache
    // design (read by tests/diagnostics to verify the anti-thrash behavior).
    std::atomic<U64> statStreamOpens{0};
    std::atomic<U64> statBytesDecompressed{0}; // via unzReadCurrentFile, incl. discarded skips
    std::atomic<U64> statCacheServedBytes{0};  // bytes served from the prefix cache
    std::atomic<U64> statStreamReopens{0};    // backward-seek reopens (streaming mode only)

    void remove(BString localPath);

    static bool readFileFromZip(BString zipFile, BString file, BString& result);
    static bool extractFileFromZip(BString zipFile, BString file, BString path);
    static BString unzip(BString zipFile, BString path, std::function<void(U32, BString)> percentDone);
    static bool iterateFiles(BString zipFile, std::function<void(BString)> it);
    static bool doesFileExist(BString zipFile, BString file);

    // Walks a zip's entry names looking for the same x86_64 layout markers
    // FsZip::init uses (x86_64-linux-gnu/, x86_64-unix/, x86_64-windows/,
    // /lib64/). Stops at the first hit. Cheap enough to call from the UI
    // launcher's lookForFileSystems scan because it only reads the central
    // directory, not entry contents.
    static bool detectGuestIs64(BString zipFile);
private:
    BString deleteFilePath;
};
#endif
#endif
