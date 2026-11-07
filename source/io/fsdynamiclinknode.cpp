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
#include "fsdynamiclinknode.h"
#include "fsfilenode.h"
#include "kstat.h"

FsDynamicLinkNode::FsDynamicLinkNode(U32 id, U32 rdev, BString path, std::shared_ptr<FsNode> parent, bool isDirectory, std::function<BString(void)> fnGetLink) : FsNode(FsNode::Type::Virtual, id, rdev, path, B(""), B(""), isDirectory, parent), fnGetLink(fnGetLink) {
}

U32 FsDynamicLinkNode::rename(BString path) {
    // A dynamic link has no directory entry of its own to move.
    bw64RecoverLogOnce("dynlink-rename", "BW64-FAIL fs-dynlink op=rename outcome=refused errno=EPERM (dynamic link has no directory entry)");
    return -K_EPERM;
}

bool FsDynamicLinkNode::remove() {
    return false;
}

U64 FsDynamicLinkNode::lastModified() {
    return KSystem::getSystemTimeAsMicroSeconds() / 1000l;
}

bool FsDynamicLinkNode::isLink() {
    return true;
}

U64 FsDynamicLinkNode::length() {
    return getLink().length();
}

FsOpenNode* FsDynamicLinkNode::open(U32 flags) {
    return nullptr;
}

U32 FsDynamicLinkNode::getType(bool checkForLink) {
    return 10; // DT_LINK
}

U32 FsDynamicLinkNode::getMode() {
    // FD-195: include the type bits like every other node class -
    // without S_IFLNK, lstat consumers (systemd sd-device) do not
    // recognise the link and never resolve class symlinks, which
    // breaks parent/devpath derivation for sysfs devices.
    return K__S_IREAD | K__S_IEXEC | K__S_IWRITE | (this->getType(false) << 12);
}

U32 FsDynamicLinkNode::removeDir() {
    bw64RecoverLogOnce("dynlink-removedir", "BW64-FAIL fs-dynlink op=removeDir outcome=refused errno=EPERM (dynamic link is not a directory)");
    return -K_EPERM;
}

U32 FsDynamicLinkNode::setTimes(U64 lastAccessTime, U32 lastAccessTimeNano, U64 lastModifiedTime, U32 lastModifiedTimeNano) {
    return 0;
}

BString FsDynamicLinkNode::getLink() {
    return this->fnGetLink();
}