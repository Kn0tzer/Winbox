/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef __KRANDOM_H__
#define __KRANDOM_H__

#include <cstddef>
#include <cstdint>

// Fill bytes from the platform's nondeterministic random source. Returns false
// if that source is unavailable; callers must not substitute predictable data.
bool kernelGetRandomBytes(std::uint8_t* dst, std::size_t len);

#endif
