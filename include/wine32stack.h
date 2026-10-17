#pragma once
#include <cstdint>

// Wine 10.0 i386 syscall_frame preserves the Windows stack while Unix handlers
// use a separate stack. Layout verified against signal_i386.c and ntdll.so:
// https://github.com/wine-mirror/wine/blob/wine-10.0/dlls/ntdll/unix/signal_i386.c
struct Wine32SavedStack {
    uint32_t eip = 0, esp = 0, ebp = 0, stackBase = 0;
};

template <class Memory>
bool readWine32SavedStack(Memory& memory, uint32_t fsBase, Wine32SavedStack& result) {
    if (!fsBase || fsBase > UINT32_MAX - 0x1fc || !memory.canRead(fsBase, 0x1fc)) return false;
    if (memory.readd(fsBase + 0x18) != fsBase) return false; // NT_TIB.Self
    const uint32_t base = memory.readd(fsBase + 4), limit = memory.readd(fsBase + 8);
    const uint32_t frame = memory.readd(fsBase + 0x1f8);
    if (!frame || frame > UINT32_MAX - 0x38 || !memory.canRead(frame, 0x38)) return false;
    const uint32_t eip = memory.readd(frame + 8), esp = memory.readd(frame + 12);
    if (limit >= base || esp < limit || esp >= base || !eip || !memory.canRead(eip, 1)) return false;
    result = { eip, esp, memory.readd(frame + 0x34), base };
    return true;
}

// Follow validated frame pointers instead of mistaking arbitrary stack words
// (including ASCII text) for return addresses. Corrupt/cyclic chains stop.
template <class Memory, class Visitor>
uint32_t walkWine32SavedFrames(Memory& memory, const Wine32SavedStack& saved, Visitor visit) {
    uint32_t fp = saved.ebp, count = 0;
    while (count < 32 && saved.stackBase >= 8 && fp >= saved.esp &&
           fp <= saved.stackBase - 8 && !(fp & 3) && memory.canRead(fp, 8)) {
        const uint32_t previous = memory.readd(fp), address = memory.readd(fp + 4);
        if (!address || !memory.canRead(address, 1)) break;
        visit(fp, address); ++count;
        if (previous <= fp) break;
        fp = previous;
    }
    return count;
}
