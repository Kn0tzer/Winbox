#pragma once

#include <cstddef>
#include <cstdint>

struct GL64ClientArrayRange {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
};

inline bool gl64ComputeDrawArrayRange(std::int64_t first, std::int64_t count,
                                      GL64ClientArrayRange& range) {
    if (first < 0 || count < 0 || first > UINT32_MAX) return false;
    if (!count) { range = {0, 0}; return true; }
    const std::uint64_t last = static_cast<std::uint64_t>(first) +
                               static_cast<std::uint64_t>(count) - 1;
    if (last > UINT32_MAX) return false;
    range = {static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(last)};
    return true;
}

inline bool gl64ComputeIndexRange(const std::uint8_t* data, std::size_t dataBytes,
                                  std::size_t count, std::size_t indexBytes,
                                  GL64ClientArrayRange& range) {
    if (indexBytes != 1 && indexBytes != 2 && indexBytes != 4) return false;
    if (!count) { range = {0, 0}; return true; }
    if (!data || count > dataBytes / indexBytes) return false;

    std::uint32_t first = UINT32_MAX;
    std::uint32_t last = 0;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t value = 0;
        for (std::size_t byte = 0; byte < indexBytes; ++byte)
            value |= static_cast<std::uint32_t>(data[i * indexBytes + byte]) << (byte * 8);
        if (value < first) first = value;
        if (value > last) last = value;
    }
    range = {first, last};
    return true;
}

inline bool gl64RebaseIndexData(std::uint8_t* data, std::size_t dataBytes,
                                std::size_t count, std::size_t indexBytes,
                                std::uint32_t base) {
    GL64ClientArrayRange range;
    if (!gl64ComputeIndexRange(data, dataBytes, count, indexBytes, range) ||
        (count && range.first < base)) return false;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t value = 0;
        for (std::size_t byte = 0; byte < indexBytes; ++byte)
            value |= static_cast<std::uint32_t>(data[i * indexBytes + byte]) << (byte * 8);
        value -= base;
        for (std::size_t byte = 0; byte < indexBytes; ++byte)
            data[i * indexBytes + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    }
    return true;
}
