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
#include "x11.h"
#include "knativesystem.h"

U32 XrrConfigCurrentRate() {
    return KNativeSystem::getScreen()->screenRate();
}

bool XrrGetSize(KThread* thread, const DisplayDataPtr& displayData, U32 sizeIndex, U32& cx, U32& cy) {
    KMemory* memory = thread->memory;
    XrrData* data = displayData->xrrData;

    if (!data || !data->sizesAddress) {
        XrrGetSizes(thread, displayData, 0, 0);
        data = displayData->xrrData;
    }
    if (sizeIndex >= data->sizesCount) {
        return false;
    }

    cx = memory->readd(data->sizesAddress + sizeIndex * 16);
    cy = memory->readd(data->sizesAddress + sizeIndex * 16 + 4);
    return true;
}

U32 XrrRates(KThread* thread, const DisplayDataPtr& displayData, U32 screen, U32 sizeIndex, U32 rateCountAddress) {
    KMemory* memory = thread->memory;
    XrrData* data = displayData->xrrData;

    memory->writed(rateCountAddress, 1);
    if (data->ratesAddress) {
        return data->ratesAddress;
    }
    data->ratesAddress = thread->process->alloc(thread, 2);
    memory->writew(data->ratesAddress, XrrConfigCurrentRate());
    return data->ratesAddress;
}

U32 XrrConfigCurrentConfiguration(KThread* thread, const DisplayDataPtr& displayData, U32 rotationAddress) {
    KMemory* memory = thread->memory;
    XrrData* data = displayData->xrrData;

    if (!data || !data->sizesAddress) {
        XrrGetSizes(thread, displayData, 0, 0);
        data = displayData->xrrData;
    }
    KNativeScreenPtr screen = KNativeSystem::getScreen();
    U32 cx = screen->screenWidth();
    U32 cy = screen->screenHeight();

    memory->writed(rotationAddress, RR_Rotate_0);
    for (U32 i = 0; i < data->sizesCount; i++) {
        if (memory->readd(data->sizesAddress + i * 16) == cx && memory->readd(data->sizesAddress + i * 16 + 4) == cy) {
            return i;
        }
    }
    return 0;
}

U32 XrrGetSizes(KThread* thread, const DisplayDataPtr& data, U32 screen, U32 countAddress) {
    KMemory* memory = thread->memory;

    if (!data->xrrData) {
        data->xrrData = new XrrData();
    }
    if (data->xrrData->sizesAddress) {
        if (countAddress) {
            memory->writed(countAddress, data->xrrData->sizesCount);
        }
        return data->xrrData->sizesAddress;
    }
    U32 desktopCx = 0;
    U32 desktopCy = 0;
    KNativeSystem::getScreenDimensions(&desktopCx, &desktopCy);
    std::vector<std::pair<U32, U32>> modes;
    auto addMode = [&modes](U32 width, U32 height) {
        if (!width || !height) return;
        for (const auto& mode : modes) {
            if (mode.first == width && mode.second == height) return;
        }
        modes.push_back({width, height});
    };

    // Always include the active host size, including nonstandard displays under 1600px.
    addMode(desktopCx, desktopCy);
    if (desktopCx > 1600 && desktopCy > 1200) addMode(1600, 1200);
    if (desktopCx > 1280 && desktopCy > 1024) addMode(1280, 1024);
    addMode(1024, 768);
    addMode(800, 600);
    addMode(640, 480);

    U32 count = (U32)modes.size();
    U32 sizes = thread->process->alloc(thread, sizeof(XRRScreenSize) * count);
    data->xrrData->sizesAddress = sizes;
    data->xrrData->sizesCount = count;
    for (U32 i = 0; i < count; i++) {
        U32 address = sizes + sizeof(XRRScreenSize) * i;
        memory->writed(address, modes[i].first);
        memory->writed(address + 4, modes[i].second);
        memory->writed(address + 8, 0);
        memory->writed(address + 12, 0);
    }
    if (countAddress) {
        memory->writed(countAddress, data->xrrData->sizesCount);
    }
    return data->xrrData->sizesAddress;
}
