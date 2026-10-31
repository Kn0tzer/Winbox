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

#include "boxedwine.h"
#ifdef BOXEDWINE_ZLIB
#include "fszipnode.h"
#include "fsfilenode.h"
#include UNISTD
#include <fcntl.h>
#include "fszipopennode.h"
#include UTIME

FsZipNode::FsZipNode(const fsZipInfo& zipInfo, const std::shared_ptr<FsZip>& fsZip) : fsZip(fsZip) {
    this->zipInfo = zipInfo;
}

bool FsZipNode::moveToFileSystem(std::shared_ptr<FsNode> node) {
    if (node->isDirectory())
        return false;
    if (node->isLink()) {
        // The old code never checked this open: on failure it wrote to and
        // closed an invalid handle and still reported success, so the
        // caller believed the link existed on the host when nothing had
        // been written.
        U32 to = ::open((node->nativePath+EXT_LINK).c_str(), O_WRONLY | O_CREAT | O_BINARY, 0666);
        if (to == 0xFFFFFFFF) {
            klog_fmt("BW64-FAIL zip-extract entry=%s dest=%s errno=%d outcome=link not written", node->path.c_str(), (node->nativePath+EXT_LINK).c_str(), errno);
            return false;
        }
        bool wrote = ::write(to, node->link.c_str(), node->link.length()) == (int)node->link.length();
        ::close(to);
        if (!wrote) {
            klog_fmt("BW64-FAIL zip-extract entry=%s dest=%s errno=%d outcome=partial link removed", node->path.c_str(), (node->nativePath+EXT_LINK).c_str(), errno);
            unlink((node->nativePath+EXT_LINK).c_str());
            return false;
        }
        return true;
    }

    FsOpenNode* from = this->open(node, K_O_RDONLY);
    bool result = false;
    U32 to = ::open(node->nativePath.c_str(), O_WRONLY | O_CREAT | O_BINARY, 0666);
    if (to != 0xFFFFFFFF) {
        U8 buffer[4096];
        U64 copied = 0;
        bool writeFailed = false;
        U32 read = from->readNative(buffer, 4096);
        while (read) {
            if (::write(to, buffer, read) != (int)read) {
                writeFailed = true;
                break;
            }
            copied += read;
            read = from->readNative(buffer, 4096);
        }
        ::close(to);
        if (writeFailed || copied != this->zipInfo.length) {
            // A partial extraction left at nativePath is served as the real
            // file from now on (the node checks existence, not length), so
            // remove it and let a later open retry from the zip instead.
            // The old code also leaked `to` on the write-failure path.
            klog_fmt("BW64-FAIL zip-extract entry=%s dest=%s copied=%llu expected=%llu outcome=partial file removed", node->path.c_str(), node->nativePath.c_str(), (unsigned long long)copied, (unsigned long long)this->zipInfo.length);
            unlink(node->nativePath.c_str());
        } else {
            struct utimbuf settime = { 0, 0 };

            settime.actime = this->zipInfo.lastModified / 1000;
            settime.modtime = this->zipInfo.lastModified / 1000;
            utime(node->nativePath.c_str(), &settime);

            result = true;
        }
    } else {
        klog_fmt("BW64-FAIL zip-extract entry=%s dest=%s errno=%d outcome=file not extracted", node->path.c_str(), node->nativePath.c_str(), errno);
    }
    from->close();
    return result;
}

U64 FsZipNode::lastModified() {
    return this->zipInfo.lastModified;
}

U64 FsZipNode::length() {
    return this->zipInfo.length;
}

FsOpenNode* FsZipNode::open(std::shared_ptr<FsNode> node, U32 flags) {
    std::shared_ptr<FsZipNode> zipNode = shared_from_this();
    return new FsZipOpenNode(node, zipNode, flags, (U64)this->zipInfo.offset);
}

#endif
