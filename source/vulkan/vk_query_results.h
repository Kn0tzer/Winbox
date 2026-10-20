#ifndef BW_VK_QUERY_RESULTS_H
#define BW_VK_QUERY_RESULTS_H

#include <algorithm>
#include <cmath>
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

// Vulkan timestamps are host-clock nanoseconds divided by timestampPeriod.
// Keep equal or regressing clock samples strictly increasing across queries.
inline uint64_t timestampTicks(uint64_t hostNanoseconds, double timestampPeriod) {
    static uint64_t last = 0;
    if (!std::isfinite(timestampPeriod) || timestampPeriod <= 0.0) timestampPeriod = 1.0;
    const long double ticks = static_cast<long double>(hostNanoseconds) / timestampPeriod;
    uint64_t value = ticks >= static_cast<long double>(std::numeric_limits<uint64_t>::max())
        ? std::numeric_limits<uint64_t>::max() : static_cast<uint64_t>(ticks);
    if (value <= last && last != std::numeric_limits<uint64_t>::max()) value = last + 1;
    last = value;
    return value;
}

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
                                    double timestampPeriod, uint64_t hostNanoseconds) {
    EncodedResults out;
    if ((queryType != VKQUERY_OCCLUSION && queryType != VKQUERY_TIMESTAMP) ||
        firstQuery > poolCount || queryCount > poolCount - firstQuery) return out;
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
        // Occlusion is a nonzero placeholder; precise mode still reports an
        // approximate sample count because this bridge does not rasterize queries.
        const uint64_t value = queryType == VKQUERY_OCCLUSION
            ? 1 : timestampTicks(hostNanoseconds, timestampPeriod);
        writeUnsigned(out.bytes, offset, value, width);
        if (flags & VKQUERY_WITH_AVAILABILITY)
            writeUnsigned(out.bytes, offset + width, 1, width);
        out.countWritten++;
    }
    return out;
}

} // namespace vkquery

#endif
