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

#include "bufferaccess.h"

#include <stdio.h>
#include <string.h>

FsOpenNode* openProcStat(const std::shared_ptr<FsNode>& node, U32 flags, U32 data) {
    // The guest topology has exactly one CPU (KSystem::guestCpuCount), so a
    // host /proc/stat passthrough must not leak cpu1..cpuN lines: keep the
    // aggregate line, re-emit it as cpu0, and pass the non-cpu lines through.
    BString raw = Platform::procStat();
    BString aggregate;
    std::vector<BString> lines;
    raw.split('\n', lines);
    for (const BString& line : lines) {
        if (line.startsWith("cpu ")) {
            aggregate = line;
        }
    }
    if (aggregate.length()) {
        BString result;
        result += aggregate; result += "\n";
        result += B("cpu0") + (aggregate.c_str() + 3); result += "\n";
        for (const BString& line : lines) {
            if (line.length() && !line.startsWith("cpu")) {
                result += line; result += "\n";
            }
        }
        return new BufferAccess(node, flags, result);
    }
    return new BufferAccess(node, flags, raw);
}
