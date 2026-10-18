// Shared table-slot namespace for guest32 and guest64 generated Wasm.
#ifndef BW_JIT_TABLE_H
#define BW_JIT_TABLE_H

#include <cstdint>

// In pthread builds this storage is in shared linear memory. Both backends
// reserve from it, even though each worker installs into its own Wasm table.
extern int32_t bwJitTableNextSlot;

// Reserve unique slots and grow the calling worker's table. Failed growth
// leaves a hole: rolling back could collide with a concurrent reservation.
int bwJitReserveTableSlots(uint32_t count);

#endif
