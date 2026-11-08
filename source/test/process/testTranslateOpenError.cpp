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
 *  along with this program if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef __TEST

#include <cerrno>

#include "../cpu/testCPU.h"

// Frontier review D17: KProcess::openFileDescriptor translates a
// failed node->open() through translateOpenError(), which used to
// collapse every errno it did not list to EINVAL. The fs layer
// preserves the host errno exactly across the failed open
// (FsFileNode::open restores it before returning nullptr), so the
// map now covers the full set open(2) can answer with; errnos with
// no open(2) meaning stay on the EINVAL default.
U32 translateOpenError();

namespace {
struct OpenErrorRow {
    int hostErrno;
    S32 expected;
};
} // namespace

void testTranslateOpenError() {
    const OpenErrorRow rows[] = {
        // Listed before the widening (controls).
        {EACCES, -K_EACCES},
        {EEXIST, -K_EEXIST},
        {ENOENT, -K_ENOENT},
        {EISDIR, -K_EISDIR},
        // Widened by D17.
        {EAGAIN, -K_EAGAIN},
        {EBUSY, -K_EBUSY},
        {EFAULT, -K_EFAULT},
        {EFBIG, -K_EFBIG},
        {EINTR, -K_EINTR},
        {EIO, -K_EIO},
        {ELOOP, -K_ELOOP},
        {EMFILE, -K_EMFILE},
        {EMLINK, -K_EMLINK},
        {ENAMETOOLONG, -K_ENAMETOOLONG},
        {ENFILE, -K_ENFILE},
        {ENODEV, -K_ENODEV},
        {ENOMEM, -K_ENOMEM},
        {ENOSPC, -K_ENOSPC},
        {ENOTDIR, -K_ENOTDIR},
        {ENXIO, -K_ENXIO},
        {EOVERFLOW, -K_EOVERFLOW},
        {EPERM, -K_EPERM},
        {EROFS, -K_EROFS},
        {ETXTBSY, -K_ETXTBSY},
        {EXDEV, -K_EXDEV},
        // No open(2) meaning: the EINVAL default stays.
        {EBADF, -K_EINVAL},
        {EINVAL, -K_EINVAL},
        {ESRCH, -K_EINVAL},
    };
    for (const OpenErrorRow& row : rows) {
        errno = row.hostErrno;
        S32 got = (S32)translateOpenError();
        if (got != row.expected) {
            testFail("translateOpenError(errno=%d) answered %d, expected %d", row.hostErrno, got, row.expected);
            return;
        }
    }
}

#endif
