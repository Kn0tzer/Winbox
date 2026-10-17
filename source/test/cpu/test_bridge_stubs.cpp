// Stub bridge implementations for the `make test` build.
// The test build doesn't compile the real gl64bridge.cpp/vk64bridge.cpp
// (they need BOXEDWINE_GUEST_X64 + OpenGL). These stubs let syscall.cpp
// link, and return -K_ENOSYS so the Stage A test can verify the trap
// dispatcher recognizes the magic numbers.
#include "boxedwine.h"

U32 gl32Bridge(CPU* cpu, U32 fnId, U32 argsAddr) {
    (void)cpu; (void)fnId; (void)argsAddr;
    return (U32)-K_ENOSYS;
}

U64 vk32Bridge(CPU* cpu, U32 fnId, U32 argsAddr) {
    (void)cpu; (void)fnId; (void)argsAddr;
    return (U64)(U32)-K_ENOSYS;
}

// msgScratch is defined in syscall64.cpp which requires BOXEDWINE_GUEST_X64.
// Provide a stub for the test build.
U32 msgScratch(KThread* thread) {
    (void)thread;
    return 0;
}

// Stubs for SDL mainloop functions not in test build
bool hasMainThreadWork() { return false; }
void tickXWirePresent() {}
