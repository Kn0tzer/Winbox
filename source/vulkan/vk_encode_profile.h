#ifndef VK_ENCODE_PROFILE_H
#define VK_ENCODE_PROFILE_H
#include <array>
#include <cstdint>

// Debug-only nested scopes. Exclusive spans partition the outer scope's wall
// exactly; inclusive spans explain callers without double-counting children.
namespace vkencode {
enum Phase : unsigned {
    Submit, Serialize, Image, ImageView, ImageDedup, ImageLookup, ImageRead,
    ImageHash, ImageRecord, Buffer, Sampler, Descriptors, Pipeline, Shader,
    RecordFill, RecordAppend, Queue, Transfer, ReadbackSetup, ReadbackWait, Count
};
inline constexpr const char* names[Count] = {
    "submit", "serialize", "image", "imageView", "imageDedup", "imageLookup", "imageRead",
    "imageHash", "imageRecord", "buffer", "sampler", "descriptors", "pipeline", "shader",
    "recordFill", "recordAppend", "queue", "transfer", "readbackSetup", "readbackWait"
};
template<class Clock> struct Profile {
    std::array<double, Count> exclusive{}, inclusive{};
    std::array<std::uint64_t, Count> calls{};
    Phase current = Count;
    double last = 0, rootMs = 0;
    void reset() { *this = Profile{}; }
    struct Scope {
        Profile* p = nullptr;
        Phase phase = Count, parent = Count;
        double start = 0;
        Scope(Profile& profile, Phase which, bool enabled) {
            if (!enabled) return;
            p = &profile; phase = which; parent = p->current;
            start = Clock::now();
            if (parent != Count) p->exclusive[parent] += start - p->last;
            p->current = which; p->last = start; ++p->calls[which];
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        void stop() {
            if (!p) return;
            const double end = Clock::now();
            p->exclusive[phase] += end - p->last;
            p->inclusive[phase] += end - start;
            if (parent == Count) p->rootMs += end - start;
            p->last = end; p->current = parent; p = nullptr;
        }
        ~Scope() { stop(); }
    };
};
}
#endif
