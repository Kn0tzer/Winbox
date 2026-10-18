#include "bwjittable.h"

alignas(4) int32_t bwJitTableNextSlot = 0;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(int, bw_jit_reserve_slots_impl, (int32_t* counter, uint32_t count), {
    if (!count || count > 0x3fffffff) return -1;
    var slots = new Int32Array(HEAPU8.buffer, counter, 1);
    var first;
    if (typeof SharedArrayBuffer !== 'undefined' && HEAPU8.buffer instanceof SharedArrayBuffer) {
        Atomics.compareExchange(slots, 0, 0, wasmTable.length);
        first = Atomics.add(slots, 0, count);
    } else {
        if (!slots[0]) slots[0] = wasmTable.length;
        first = slots[0];
        slots[0] += count;
    }
    if (first <= 0 || first + count > 0x7fffffff) return -1;
    try {
        if (first + count > wasmTable.length)
            wasmTable.grow(first + count - wasmTable.length);
        return first;
    } catch (error) {
        return -1;
    }
});

int bwJitReserveTableSlots(uint32_t count) {
    return bw_jit_reserve_slots_impl(&bwJitTableNextSlot, count);
}
#else
int bwJitReserveTableSlots(uint32_t) { return -1; }
#endif
