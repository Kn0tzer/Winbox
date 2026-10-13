// GPL-2.0-or-later. Guest memory for the shared 32/64-bit OpenGL renderer.
#ifndef GL_GUEST_MEMORY_H
#define GL_GUEST_MEMORY_H

#include <vector>

// Keep one renderer implementation. Only guest-memory access depends on the
// CPU architecture; GL arguments use the same 64-bit slots in both shims.
class GLGuestMemory {
public:
    explicit GLGuestMemory(KMemory* memory) : memory32(memory), memory64(nullptr) {}
    explicit GLGuestMemory(KMemory64* memory) : memory32(nullptr), memory64(memory) {}

    void memcpyFromGuest(void* dst, U64 address, U64 length) {
        if (memory32) memory32->memcpyFromGuest(dst, address, length);
        else memory64->memcpyFromGuest(dst, address, length);
    }
    void memcpyToGuest(U64 address, const void* src, U64 length) {
        if (memory32) memory32->memcpyToGuest(address, src, length);
        else memory64->memcpyToGuest(address, src, length);
    }
    void writed(U64 address, U32 value) {
        if (memory32) memory32->writed((U32)address, value);
        else memory64->writed(address, value);
    }
    void writeq(U64 address, U64 value) {
        if (memory32) memory32->writeq((U32)address, value);
        else memory64->writeq(address, value);
    }
    std::vector<U64> readPointers(U64 address, size_t count) {
        std::vector<U64> result(count);
        if (memory32) {
            std::vector<U32> pointers(count);
            memcpyFromGuest(pointers.data(), address, count * sizeof(U32));
            for (size_t i = 0; i < count; ++i) result[i] = pointers[i];
        } else {
            memcpyFromGuest(result.data(), address, count * sizeof(U64));
        }
        return result;
    }

private:
    KMemory* memory32;
    KMemory64* memory64;
};

struct GLGuestCPU {
    GLGuestMemory* memory;
};
#endif
