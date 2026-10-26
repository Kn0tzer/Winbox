/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include "krandom.h"

#include <mutex>
#include <random>

bool kernelGetRandomBytes(std::uint8_t* dst, std::size_t len) {
    if (!dst && len) return false;
    if (!len) return true;

    static std::mutex randomMutex;
    std::lock_guard<std::mutex> lock(randomMutex);
    try {
        static std::random_device source;
        std::size_t offset = 0;
        while (offset < len) {
            const std::random_device::result_type value = source();
            for (std::size_t i = 0; i < sizeof(value) && offset < len; ++i) {
                dst[offset++] = (std::uint8_t)(value >> (i * 8));
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}
