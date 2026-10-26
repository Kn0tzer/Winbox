#ifndef BW_VK_QUERY_RESULTS_H
#define BW_VK_QUERY_RESULTS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace vkquery {

constexpr uint32_t VKQUERY_OCCLUSION = 0;
constexpr uint32_t VKQUERY_TIMESTAMP = 2;
constexpr uint32_t VKQUERY_64_BIT = 0x1;
constexpr uint32_t VKQUERY_WAIT = 0x2;
constexpr uint32_t VKQUERY_WITH_AVAILABILITY = 0x4;
constexpr uint32_t VKQUERY_PARTIAL = 0x8;

struct EncodedResults {
    std::vector<uint8_t> bytes;
    bool valid = false;
    bool complete = false;
    uint32_t countWritten = 0;
    size_t resultBytes = 0;
    uint64_t strideBytes = 0;
};

inline void writeUnsigned(std::vector<uint8_t>& bytes, size_t offset, uint64_t value, size_t width) {
    for (size_t i = 0; i < width && offset + i < bytes.size(); ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

inline uint64_t readUnsigned(const std::vector<uint8_t>& bytes, size_t offset, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width && offset + i < bytes.size(); ++i)
        value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
    return value;
}

inline EncodedResults encodeResults(uint32_t queryType, uint32_t firstQuery,
                                    uint32_t queryCount, uint32_t poolCount,
                                    size_t dataSize, uint64_t stride, uint32_t flags,
                                    const std::vector<uint64_t>& values,
                                    const std::vector<uint8_t>& availability) {
    EncodedResults out;
    if ((queryType != VKQUERY_OCCLUSION && queryType != VKQUERY_TIMESTAMP) ||
        firstQuery > poolCount || queryCount > poolCount - firstQuery ||
        values.size() < poolCount || availability.size() < poolCount) return out;
    const size_t width = (flags & VKQUERY_64_BIT) ? 8 : 4;
    const size_t resultSize = width * ((flags & VKQUERY_WITH_AVAILABILITY) ? 2 : 1);
    if (!stride) stride = resultSize;
    if (queryCount > 1 && stride < resultSize) return out;
    if (queryCount && (queryCount - 1) > (std::numeric_limits<uint64_t>::max() - resultSize) / stride) return out;
    const uint64_t requiredBytes = queryCount ? static_cast<uint64_t>(queryCount - 1) * stride + resultSize : 0;
    out.bytes.resize(static_cast<size_t>(std::min<uint64_t>(requiredBytes, dataSize)), 0);
    out.resultBytes = resultSize;
    out.strideBytes = stride;
    out.valid = true;
    out.complete = requiredBytes <= dataSize;
    for (uint32_t i = 0; i < queryCount; ++i) {
        const uint64_t offset64 = static_cast<uint64_t>(i) * stride;
        if (offset64 > out.bytes.size() || resultSize > out.bytes.size() - static_cast<size_t>(offset64)) {
            out.complete = false;
            break;
        }
        const size_t offset = static_cast<size_t>(offset64);
        const uint32_t index = firstQuery + i;
        const bool available = availability[index] != 0;
        if (!available && !(flags & VKQUERY_PARTIAL)) {
            out.complete = false;
            break;
        }
        const uint64_t value = values[index];
        writeUnsigned(out.bytes, offset, available ? value : 0, width);
        if (flags & VKQUERY_WITH_AVAILABILITY)
            writeUnsigned(out.bytes, offset + width, available ? 1 : 0, width);
        out.countWritten++;
    }
    return out;
}

} // namespace vkquery

#endif
