#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// Host diagnostics must not split a guest's partial terminal line. Buffers are
// separate for each producer even when forked processes share a terminal fd.
class GuestLogLines {
public:
    static constexpr size_t maxLineBytes = 4096;
    static constexpr size_t maxStreams = 256;

    std::vector<std::string> append(uint32_t producer, std::string_view bytes) {
        std::vector<std::string> records;
        std::lock_guard<std::mutex> lock(mutex);
        if (!pending.count(producer) && pending.size() == maxStreams) {
            auto old = pending.begin();
            if (!old->second.empty()) records.push_back(std::move(old->second));
            pending.erase(old);
        }
        auto& line = pending[producer];
        for (char byte : bytes) {
            line.push_back(byte);
            if (byte == '\n' || line.size() == maxLineBytes) {
                records.push_back(std::move(line));
                line.clear();
            }
        }
        if (line.empty()) pending.erase(producer);
        return records;
    }

private:
    std::mutex mutex;
    std::map<uint32_t, std::string> pending;
};
