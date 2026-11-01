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
#include "fsmemnode.h"
#include "fsmemopennode.h"
#include "kstat.h"

FsMemNode::FsMemNode(U32 id, U32 rdev, BString path) : FsNode(Type::Memory, id, rdev, path, B(""), B(""), false, nullptr), openNode(nullptr) {
}

// A memfd node has no directory entry: it is created by memfd_create and
// handed out as a descriptor, never linked into the VFS tree. The FsNode
// operations that act on directory entries therefore cannot be served.
// They refuse with EPERM and report once per operation (the disposition
// FsVirtualNode::removeDir established for the same situation) instead of
// panicking: these are guest-facing vtable slots, and a panic here would
// turn a guest rename/unlink/open attempt into an emulator death the
// moment any path ever resolves to this node type.
U32 FsMemNode::rename(BString path) {
    bw64RecoverLogOnce("fsmemnode-rename", "BW64-FAIL fs-memnode op=rename outcome=refused errno=EPERM (memfd node has no directory entry)");
    return -K_EPERM;
}

bool FsMemNode::remove() {
    bw64RecoverLogOnce("fsmemnode-remove", "BW64-FAIL fs-memnode op=remove outcome=refused errno=EPERM (memfd node has no directory entry)");
    return false;
}

U64 FsMemNode::lastModified() {
    return this->openNode ? this->openNode->lastModified() : 0;
}

U64 FsMemNode::length() {
    return this->openNode ? this->openNode->length() : 0;
}

FsOpenNode* FsMemNode::open(U32 flags) {
    bw64RecoverLogOnce("fsmemnode-open", "BW64-FAIL fs-memnode op=open outcome=refused (memfd is descriptor-only; open by path is not possible)");
    return nullptr;
}

U32 FsMemNode::getType(bool checkForLink) {
    return 8; // DT_REG
}

U32 FsMemNode::getMode() {
    return K__S_IWRITE | K__S_IREAD | (this->getType(false) << 12);
}

U32 FsMemNode::removeDir() {
    bw64RecoverLogOnce("fsmemnode-removedir", "BW64-FAIL fs-memnode op=removeDir outcome=refused errno=EPERM (memfd node has no directory entry)");
    return -K_EPERM;
}

U32 FsMemNode::setTimes(U64 lastAccessTime, U32 lastAccessTimeNano, U64 lastModifiedTime, U32 lastModifiedTimeNano) {
    bw64RecoverLogOnce("fsmemnode-settimes", "BW64-FAIL fs-memnode op=setTimes outcome=refused errno=EPERM (memfd node stores no timestamps)");
    return -K_EPERM;
}