// 32-bit OpenGL bridge ABI.
//
// The 32-bit guest traps via int $0x80 with EAX=GL32_SYSCALL_NR,
// EBX=fnId, ECX=guest VA of GL32Args. Function IDs are SHARED with the
// 64-bit ABI (gl64bridge_abi.h) — only the guest-side marshaling differs.
//
// GL32Args has the identical layout to GL64Args (16 x u64); the 32-bit
// guest zero-extends 32-bit values into the slots.

#ifndef __GL32BRIDGE_ABI_H__
#define __GL32BRIDGE_ABI_H__

#include <stdint.h>
#include "gl64bridge_abi.h"

// Same syscall number as 64-bit ('GL' = 0x474C). The 32-bit syscall path
// (ksyscall, EAX=0x474C0000) dispatches to gl32Bridge.
#define GL32_SYSCALL_NR  ((uint32_t)0x474C0000U)

#define GL32_MAX_ARGS 16
typedef struct GL32Args {
    uint64_t a[GL32_MAX_ARGS];
} GL32Args;

#endif // __GL32BRIDGE_ABI_H__
