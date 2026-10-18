/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#include "../decoder.h"
#include "normalCPU.h"
#include "../../softmmu/soft_code_page.h"
#include "../../softmmu/kmemory_soft.h"
#include "../armv7/armv7CPU.h"
#include "../armv8/armv8CPU.h"
#include "../../../util/ptrpool.h"
#include "../wasm/wasmMemoryOrdering.h"
#if defined(BOXEDWINE_JIT) && !defined(BOXEDWINE_WASM_JIT)
#include "../jit/jitCodeGen.h"
#endif

#if defined(BOXEDWINE_MULTI_THREADED) && defined(BOXEDWINE_WASM_JIT)
static inline void normalOrderMemoryInstruction(DecodedOp* op) {
    // Match the JIT's guest-memory instruction boundaries during warmup and
    // single-op fallback. Ordinary Wasm loads/stores do not provide x86 TSO
    // on weakly ordered hosts, even when another access uses a locked op.
    if (instructionInfo[op->inst].readMemWidth || instructionInfo[op->inst].writeMemWidth) {
        if (wasmJitNeedsMemoryFence) {
            std::atomic_thread_fence(std::memory_order_seq_cst);
        } else {
            // Retain the C++ compiler barrier even without hardware fences.
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
    }
}
#define START_OP(cpu, op) normalOrderMemoryInstruction(op)
#elif defined(BOXEDWINE_MULTI_THREADED)
#ifdef _DEBUG
//#define START_OP(cpu, op) op->log(cpu)
#define START_OP(cpu, op)
#else
#define START_OP(cpu, op)
#endif
#else
#ifdef _DEBUG
#define START_OP(cpu, op) cpu->blockInstructionCount++; op->log(cpu)
 //#define START_OP(cpu, op)
#else
#define START_OP(cpu, op) cpu->blockInstructionCount++
#endif
#endif

// Direct-dispatch builds use normalDispatch so the switch generates direct
// return_call instructions per opcode rather than one return_call_indirect.
// JIT builds keep pfn dispatch because pfn may be a JIT trampoline.
// Forward declaration for Callback handling in NEXT() (WASM fix 2026-10-10)
void OPCALL onExitSignal(CPU* cpu, DecodedOp* op);
#ifdef BOXEDWINE_DIRECT_NORMAL_DISPATCH
#define NEXT() cpu->eip.u32+=op->len; MUSTTAIL return normalDispatch(cpu, op->next);
#elif defined(BOXEDWINE_WASM_JIT)
// Non-bridge interpreter chains go through normalDispatch's switch (direct
// tail calls) instead of the indirect nextOp->pfn call. Chains exit to run()
// at compiled-block heads so wasmStartJITOp keeps its machinery (per-worker
// readiness, relocBase, runCount counting); branches still break chains in
// NEXT_BRANCH1/2 - that is the stack bound and must stay (the historical
// branch-through variant overflowed the JS stack). Any DecodedOp whose pfn is
// not normalOps[inst] must keep an inst that lands in normalDispatch's
// default: case (see cpu.cpp's lastOp).
#define NEXT_INTERP_CHAIN()                                                                         \
    if (nextOp && nextOp->pfn != cpu->thread->process->startJITOp) {                                \
        if (nextOp->inst == Callback) {                                                             \
            cpu->nextOp = nextOp;                                                                   \
            return;                                                                                 \
        }                                                                                           \
        MUSTTAIL return normalDispatch(cpu, nextOp);                                                \
    }                                                                                                \
    cpu->nextOp = nextOp;                                                                            \
    return
#define NEXT() do {                                                                                 \
    cpu->eip.u32 += op->len;                                                                         \
    DecodedOp* nextOp = op->next ? op->next : cpu->getNextOp();                                      \
    NEXT_INTERP_CHAIN();                                                                             \
} while (0)
#else
#define NEXT() cpu->eip.u32+=op->len; MUSTTAIL return (op->next->inst == Callback ? onExitSignal(cpu, op->next) : op->next->pfn(cpu, op->next));
#endif
#define NEXT_DONE() cpu->nextOp = cpu->getNextOp();
#define NEXT_DONE_JUMP_OR_CALL() cpu->nextOp = cpu->getNextOp(OP_FLAG2_JUMP_TARGET);

#if defined(BOXEDWINE_DIRECT_NORMAL_DISPATCH) || defined(BOXEDWINE_WASM_JIT)
static inline bool normalGetZF(CPU* cpu) {
    switch (cpu->lazyFlagType) {
    case FLAGS_NONE:
        return (cpu->flags & ZF) != 0;
    case FLAGS_ADD8:
    case FLAGS_OR8:
    case FLAGS_AND8:
    case FLAGS_SUB8:
    case FLAGS_XOR8:
    case FLAGS_INC8:
    case FLAGS_DEC8:
    case FLAGS_CMP8:
    case FLAGS_TEST8:
        return cpu->result.u8 == 0;
    case FLAGS_ADD16:
    case FLAGS_OR16:
    case FLAGS_AND16:
    case FLAGS_SUB16:
    case FLAGS_XOR16:
    case FLAGS_INC16:
    case FLAGS_DEC16:
    case FLAGS_CMP16:
    case FLAGS_TEST16:
        return cpu->result.u16 == 0;
    case FLAGS_ADD32:
    case FLAGS_OR32:
    case FLAGS_AND32:
    case FLAGS_SUB32:
    case FLAGS_XOR32:
    case FLAGS_INC32:
    case FLAGS_DEC32:
    case FLAGS_CMP32:
    case FLAGS_TEST32:
        return cpu->result.u32 == 0;
    default:
        return cpu->getZF();
    }
}
static inline bool normalGetCF(CPU* cpu) {
    switch (cpu->lazyFlagType) {
    case FLAGS_NONE:
        return (cpu->flags & CF) != 0;
    case FLAGS_ADD8:
        return cpu->result.u8 < cpu->dst.u8;
    case FLAGS_ADD16:
        return cpu->result.u16 < cpu->dst.u16;
    case FLAGS_ADD32:
        return cpu->result.u32 < cpu->dst.u32;
    case FLAGS_SUB8:
    case FLAGS_CMP8:
        return cpu->dst.u8 < cpu->src.u8;
    case FLAGS_SUB16:
    case FLAGS_CMP16:
        return cpu->dst.u16 < cpu->src.u16;
    case FLAGS_SUB32:
    case FLAGS_CMP32:
        return cpu->dst.u32 < cpu->src.u32;
    case FLAGS_OR8:
    case FLAGS_OR16:
    case FLAGS_OR32:
    case FLAGS_AND8:
    case FLAGS_AND16:
    case FLAGS_AND32:
    case FLAGS_XOR8:
    case FLAGS_XOR16:
    case FLAGS_XOR32:
    case FLAGS_TEST8:
    case FLAGS_TEST16:
    case FLAGS_TEST32:
        return false;
    case FLAGS_INC8:
    case FLAGS_INC16:
    case FLAGS_INC32:
    case FLAGS_DEC8:
    case FLAGS_DEC16:
    case FLAGS_DEC32:
        return cpu->oldCF != 0;
    default:
        return cpu->getCF();
    }
}

static inline bool normalGetNLE(CPU* cpu) {
    switch (cpu->lazyFlagType) {
    case FLAGS_NONE:
        return !(cpu->flags & ZF) && (((cpu->flags & SF) != 0) == ((cpu->flags & OF) != 0));
    case FLAGS_CMP8:
    case FLAGS_SUB8: {
        U8 result = cpu->result.u8;
        U8 overflow = (cpu->dst.u8 ^ cpu->src.u8) & (cpu->dst.u8 ^ result) & 0x80;
        return result != 0 && ((result & 0x80) != 0) == (overflow != 0);
    }
    case FLAGS_CMP16:
    case FLAGS_SUB16: {
        U16 result = cpu->result.u16;
        U16 overflow = (cpu->dst.u16 ^ cpu->src.u16) & (cpu->dst.u16 ^ result) & 0x8000;
        return result != 0 && ((result & 0x8000) != 0) == (overflow != 0);
    }
    case FLAGS_CMP32:
    case FLAGS_SUB32: {
        U32 result = cpu->result.u32;
        U32 overflow = (cpu->dst.u32 ^ cpu->src.u32) & (cpu->dst.u32 ^ result) & 0x80000000;
        return result != 0 && ((result & 0x80000000) != 0) == (overflow != 0);
    }
    case FLAGS_OR8:
    case FLAGS_AND8:
    case FLAGS_XOR8:
    case FLAGS_TEST8:
        return cpu->result.u8 != 0 && (cpu->result.u8 & 0x80) == 0;
    case FLAGS_OR16:
    case FLAGS_AND16:
    case FLAGS_XOR16:
    case FLAGS_TEST16:
        return cpu->result.u16 != 0 && (cpu->result.u16 & 0x8000) == 0;
    case FLAGS_OR32:
    case FLAGS_AND32:
    case FLAGS_XOR32:
    case FLAGS_TEST32:
        return cpu->result.u32 != 0 && (cpu->result.u32 & 0x80000000) == 0;
    default:
        return !cpu->getZF() && cpu->getSF() == cpu->getOF();
    }
}
#else
#define normalGetZF(cpu) ((cpu)->getZF())
#define normalGetCF(cpu) ((cpu)->getCF())
#define normalGetNLE(cpu) (!(cpu)->getZF() && (cpu)->getSF() == (cpu)->getOF())
#endif

// A guest pthread already owns a host worker. Batch a bounded number of basic
// blocks before returning through the virtual run() loop. The outer loop keeps
// ownership of termination/cleanup; no cross-thread state is cached here.
// musttail bounds the host stack even for a guest loop with no syscalls.
// Cooperative and JIT backends retain their scheduler return on every branch.
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_MULTI_THREADED) && defined(BOXEDWINE_DIRECT_NORMAL_DISPATCH) && !defined(BOXEDWINE_JIT)
#define CONTINUE_BRANCH() \
    if (cpu->nextOp && static_cast<NormalCPU*>(cpu)->branchBudget > 1) { \
        --static_cast<NormalCPU*>(cpu)->branchBudget; \
        MUSTTAIL return normalDispatch(cpu, cpu->nextOp); \
    }
#else
#define CONTINUE_BRANCH()
#endif

#define NEXT_BRANCH1()                                      \
    cpu->eip.u32+=op->len;                                  \
    if (!(*(op->data.nextJump))) {                          \
        *(op->data.nextJump) = cpu->getNextOp(OP_FLAG2_JUMP_TARGET);            \
    }                                                       \
    cpu->nextOp = *(op->data.nextJump);                      \
    CONTINUE_BRANCH();

#define NEXT_BRANCH2() cpu->eip.u32+=op->len; if (!op->next) {op->next = cpu->getNextOp(); } cpu->nextOp = op->next; CONTINUE_BRANCH();

// Forward declaration so NEXT() can reference normalDispatch before the
// normal_*.h opcode handlers are included.
#if defined(BOXEDWINE_DIRECT_NORMAL_DISPATCH) || defined(BOXEDWINE_WASM_JIT)
static void OPCALL normalDispatch(CPU* cpu, DecodedOp* op);
#endif

#include "instructions.h"
#include "normal_arith.h"
#include "normal_conditions.h"
#include "normal_incdec.h"
#include "normal_pushpop.h"
#include "normal_setcc.h"
#include "normal_strings.h"
#include "normal_strings_op.h"
#include "normal_shift.h"
#include "normal_shift_op.h"
#include "normal_xchg.h"
#include "normal_bit.h"
#include "normal_mmx.h"
#include "normal_sse.h"
#include "normal_sse2.h"
#include "normal_fpu.h"
#include "normal_other.h"
#include "normal_jump.h"
#include "normal_move.h"

#include "normal_lock.h"

static OpCallback normalOps[NUMBER_OF_OPS];
static U32 normalOpsInitialized;

void OPCALL normal_sidt(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    U32 eaa = eaa(cpu, op);
    cpu->memory->writew(eaa, 1023); // limit
    cpu->memory->writed(eaa + 2, 0); // base
#ifdef _DEBUG
    klog("sidt not implemented");
#endif
    NEXT();
}

void OPCALL onTestEnd(CPU* cpu, DecodedOp* op) {
    cpu->nextOp = op;
}

// Dispatch the next decoded op by switching on its instruction ID. Using a
// switch lets WASM emit direct return_call instructions for opcode arms.
// Trap at 32-bit ntdll_wcsnicmp entry (ntdll+0xD3B0).
// PEB corruption watcher (2026-10-09): catches 32-bit PEB->ProcessParameters->Environment
// corruption when it happens, not just at crash time.
// Discovers PEB via FS:[0x30] (TEB->Peb), then PEB+0x10 (ProcessParameters),
// then PP+0x48 (Environment). Logs transitions, especially to 0x31.
// Throttled to avoid slowing emulation: checks every 4096 dispatches per CPU.

static U32 bw64PebEnvAddr = 0;
static U32 bw64PebLastEnv = 0xFFFFFFFFu;
static U32 bw64PebWatchCount = 0;
static void bw64PebWatch(CPU* cpu) {
    // Only for 32-bit processes
    if (!cpu->thread || !cpu->thread->process || cpu->thread->process->is64Bit)
        return;
    // Throttle: check every 4096 calls
    if ((++bw64PebWatchCount & 0xFFF) != 0)
        return;
    U32 fsBase = cpu->seg[4].address; // FS base = TEB
    if (fsBase < 0x1000)
        return; // FS not set up yet
    // Discover PEB address once
    if (bw64PebEnvAddr == 0) {
        U32 peb = 0, pp = 0;
        // Use try-like guards: readd may throw on bad address; wrap in checks
        // TEB->Peb at FS:[0x30]
        peb = cpu->memory->readd(fsBase + 0x30);
        if (peb < 0x10000)
            return;
        // PEB->ProcessParameters at PEB+0x10
        pp = cpu->memory->readd(peb + 0x10);
        if (pp < 0x10000)
            return;
        bw64PebEnvAddr = pp + 0x48; // RTL_USER_PROCESS_PARAMETERS.Environment
        bw64PebLastEnv = cpu->memory->readd(bw64PebEnvAddr);
        U32 tid = cpu->thread ? cpu->thread->id : 0xFFFFFFFFu;
        klog_fmt("PEBWATCH init teb=%08X peb=%08X pp=%08X envAddr=%08X env=%08X tid=%u",
                 fsBase, peb, pp, bw64PebEnvAddr, bw64PebLastEnv, tid);
        return;
    }
    // Watch for changes
    U32 env = cpu->memory->readd(bw64PebEnvAddr);
    if (env != bw64PebLastEnv) {
        U32 tid = cpu->thread ? cpu->thread->id : 0xFFFFFFFFu;
        U32 eip = cpu->eip.u32;
        klog_fmt("PEB_ENV_CHANGE old=%08X new=%08X eip=%08X tid=%u",
                 bw64PebLastEnv, env, eip, tid);
        if (env == 0x31) {
            klog_fmt("PEB_CORRUPT! Environment=0x31 at eip=%08X tid=%u", eip, tid);
            // Dump call stack
            U32 esp = cpu->reg[4].u32;
            for (int i = 0; i < 8; i++) {
                U32 addr = esp + (U32)(i * 4);
                U32 val = cpu->memory->readd(addr);
                klog_fmt("PEB_CORRUPT stack+%d val=%08X", i*4, val);
            }
        }
        bw64PebLastEnv = env;
    }
}

// ntdll.so loads at 0x10640000, so absolute = 0x1064D3B0.
// ntdll_wcsnicmp(WCHAR* s1, WCHAR* s2, SIZE_T n)
// Logs s1/s2 to catch NULL pointers that cause crash at 0xD3F8 (movzx edx,[esi]).
// CORRECTION 2026-10-09: Prior code trapped 0x1064D470 believing it was find_env_var,
// but 0xD470 is INSIDE ntdll_wcsnicmp (0xD3B0-0xD530). The FINDENV_BADENV logs were invalid.
// Only logs when env is NULL/bad (the buggy case).
static void bw64CrashTrap(CPU* cpu) {
    // Trap ntdll_wcsnicmp ENTRY at 0x1064D3B0 (ntdll.so offset 0xD3B0).
    // Prior diagnostic trapped 0x1064D470 which is INSIDE wcsnicmp, not find_env_var.
    // wcsnicmp(s1, s2, n): s1=[ESP+4], s2=[ESP+8], n=[ESP+12]
    U32 esp = cpu->reg[4].u32;
    U32 retAddr = cpu->memory->readd(esp);
    U32 s1 = cpu->memory->readd(esp + 4);       // const WCHAR* s1
    U32 s2 = cpu->memory->readd(esp + 8);       // const WCHAR* s2
    U32 n = cpu->memory->readd(esp + 12);       // SIZE_T n
    // Log when s1 or s2 is NULL (this leads to crash at 0xD3F8: movzx edx,[esi])
    if (s1 < 0x1000 || s2 < 0x1000) {
        U32 tid = cpu->thread ? cpu->thread->id : 0xFFFFFFFFu;
        U32 fevRet = cpu->memory->readd(esp + 20);
        U32 pebEnv = 0xFFFFFFFFu;
        U32 fsBase = cpu->seg[4].address;
        if (fsBase >= 0x1000) {
            U32 peb = cpu->memory->readd(fsBase + 0x30);
            if (peb >= 0x10000) {
                U32 pp = cpu->memory->readd(peb + 0x10);
                if (pp >= 0x10000) {
                    pebEnv = cpu->memory->readd(pp + 0x48);
                }
            }
        }
        klog_fmt("WCSNICMP_NULL ret=%08X s1=%08X s2=%08X n=%u esp=%08X tid=%u fevRet=%08X pebEnv=%08X",
                 retAddr, s1, s2, n, esp, tid, fevRet, pebEnv);
        // Dump stack to find caller chain (find_env_var frame and its caller)
        // At wcsnicmp entry: [ESP]=ret to find_env_var, [ESP+4]=s1, [ESP+8]=s2, [ESP+12]=n
        // The find_env_var frame is above this. Dump 16 dwords to see it.
        for (int i = 0; i < 16; i++) {
            U32 addr = esp + (U32)(i * 4);
            U32 val = cpu->memory->readd(addr);
            klog_fmt("WCSNICMP_NULL stack+%d addr=%08X val=%08X", i*4, addr, val);
        }
    }
}

#ifdef BW64_INST_TRACE
// Ring-buffer instruction trace for 32-bit bring-up debugging. Records every
// guest instruction dispatched by normalDispatch (EIP, GPRs, FS base, raw
// bytes, thread id); the tail is dumped on page fault. Debug only.
struct Bw64TraceEntry {
    U32 eip;
    U32 tid;
    U32 regs[8]; // EAX ECX EDX EBX ESP EBP ESI EDI
    U32 fsBase;
    U8 len;
    U8 bytes[15];
};
#include <atomic>
// Per-tid rings: isolates each thread's history so heavy threads cannot
// evict the faulting thread's tail.
static const U32 BW64_TRACE_N = 1024;
static const U32 BW64_TRACE_TIDS = 64;
static Bw64TraceEntry bw64TraceBuf[BW64_TRACE_TIDS][BW64_TRACE_N];
static std::atomic<U32> bw64TraceHead[BW64_TRACE_TIDS];
static volatile U32 bw64TraceActive = 1;
static U32 bw64TidCount[256] = {0};

// Targeted RIP-hit logging for 32-bit ntdll 0xD3F8 investigation (2026-10-08).
// Logs control flow through run_wineboot NtCreateEvent / STATUS_OBJECT_NAME_EXISTS
// (0x40000000) check and the set_env_var call site. Inst build only.
static void bw64RipHitLog(CPU* cpu) {
    U32 eip = cpu->eip.u32;
    if (eip == 0x1064F966) {
        klog_fmt("RIPHIT JE_SITE eax=%08X", cpu->reg[0].u32);
    } else if (eip == 0x1064F96C) {
        klog_fmt("RIPHIT JE_NOT_TAKEN");
    } else if (eip == 0x106508F6) {
        klog_fmt("RIPHIT JE_TAKEN");
    } else if (eip == 0x1064FBAC) {
        U32 esp = cpu->reg[4].u32;
        U32 slot = cpu->memory->readd(esp + 0x3C);
        klog_fmt("RIPHIT PUSH_NAME esp=%08X name_slot=%08X", esp, slot);
    } else if (eip == 0x1064FBB4) {
        klog_fmt("RIPHIT CALL_SETENV");
    }
}

static void bw64TraceRecord(CPU* cpu, DecodedOp* op) {
    bw64RipHitLog(cpu);
    if (!bw64TraceActive)
        return;
    U32 rtid = cpu->thread ? cpu->thread->id : 0xFFFFFFFFu;
    bw64TidCount[rtid & 255]++;
    U32 t = rtid & (BW64_TRACE_TIDS - 1);
    U32 h = bw64TraceHead[t].fetch_add(1, std::memory_order_relaxed);
    Bw64TraceEntry* e = &bw64TraceBuf[t][h & (BW64_TRACE_N - 1)];
    e->eip = cpu->eip.u32;
    e->tid = rtid;
    for (int i = 0; i < 8; i++)
        e->regs[i] = cpu->reg[i].u32;
    e->fsBase = cpu->seg[4].address;
    U32 len = op->len;
    if (len > 15)
        len = 15;
    e->len = (U8)len;
    for (U32 i = 0; i < len; i++)
        e->bytes[i] = cpu->memory->readb(cpu->eip.u32 + i);
}

void bw64DumpInstTrace(U32 tid) {
    bw64TraceActive = 0; // stop all recording while we dump
    U32 t = tid & (BW64_TRACE_TIDS - 1);
    U32 total = bw64TraceHead[t].load(std::memory_order_relaxed);
    U32 avail = total < BW64_TRACE_N ? total : BW64_TRACE_N;
    U32 dumpN = avail;
    static Bw64TraceEntry snap[1024];
    for (U32 n = 0; n < dumpN; n++)
        snap[n] = bw64TraceBuf[t][(total - dumpN + n) & (BW64_TRACE_N - 1)];
    klog_fmt("INSTTRACE total=%u avail=%u dump=%u tid=%u", total, avail, dumpN, tid);
    for (U32 t = 0; t < 256; t++)
        if (bw64TidCount[t])
            klog_fmt("INSTTRACE count tid=%u n=%u", t, bw64TidCount[t]);
    char hex[33];
    // tid histogram over dumped window (diagnoses tid mismatch)
    U32 histTid[8] = {0}, histCnt[8] = {0};
    for (U32 n = 0; n < dumpN; n++) {
        Bw64TraceEntry* e = &snap[n];
        U32 h = 0;
        for (; h < 8; h++) {
            if (histTid[h] == e->tid || histCnt[h] == 0) break;
        }
        if (h < 8) { histTid[h] = e->tid; histCnt[h]++; }
    }
    for (U32 h = 0; h < 8 && histCnt[h]; h++)
        klog_fmt("INSTTRACE hist tid=%u count=%u", histTid[h], histCnt[h]);
    for (U32 ni = 0; ni < dumpN; ni++) {
        U32 n = dumpN - 1 - ni; // newest first: transport drops tail under flood
        Bw64TraceEntry* e = &snap[n];
        for (U32 b = 0; b < e->len; b++) {
            U8 v = e->bytes[b];
            hex[b * 2] = "0123456789ABCDEF"[v >> 4];
            hex[b * 2 + 1] = "0123456789ABCDEF"[v & 15];
        }
        hex[e->len * 2] = 0;
        klog_fmt("T %08X tid=%u esp=%08X ebp=%08X eax=%08X ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X fs=%08X %s",
                 e->eip, e->tid, e->regs[4], e->regs[5], e->regs[0], e->regs[1], e->regs[2],
                 e->regs[3], e->regs[6], e->regs[7], e->fsBase, hex);
    }
    klog("INSTTRACE end");
    bw64TraceActive = 1; // re-enable for subsequent faults on other threads
}
#else
void bw64DumpInstTrace(U32) {}
#endif


// Crash-site trap for 32-bit 0xD3F8 (2026-10-09): fires ONLY when EIP==0x1064D3F8
// AND ESI==0 (the actual crash condition). The EBP=0xB4B was a false positive
// (legitimate LEA in libc). The real bug is ESI=0 (NULL pointer dereference).
// Logs the caller return address and stack to identify who passed NULL.
// Diagnostic only - do NOT commit.
static void bw64CrashSiteTrap(CPU* cpu) {
    static bool fired = false;
    U32 eip = cpu->eip.u32;
    U32 esi = cpu->reg[6].u32;
    // Only fire at the crash site with NULL ESI
    if (eip == 0x1064D3F8 && esi == 0 && !fired) {
        U32 esp = cpu->reg[4].u32;
        U32 tid = cpu->thread ? cpu->thread->id : 0xFFFFFFFFu;
        // Return address is at [ESP] (cdecl/stdcall)
        U32 retAddr = cpu->memory->readd(esp);
        klog_fmt("CRASHSITE eip=%08X tid=%u esi=0 (NULL!) esp=%08X retAddr=%08X eax=%08X ebx=%08X ecx=%08X edx=%08X ebp=%08X edi=%08X",
                 eip, tid, esp, retAddr,
                 cpu->reg[0].u32, cpu->reg[3].u32, cpu->reg[1].u32, cpu->reg[2].u32,
                 cpu->reg[5].u32, cpu->reg[7].u32);
        // Dump 32 bytes of instruction at EIP to identify the faulting instruction
        char ihex[65];
        for (U32 b = 0; b < 32; b++) {
            U8 v = cpu->memory->readb(eip + b);
            ihex[b*2] = "0123456789ABCDEF"[v >> 4];
            ihex[b*2+1] = "0123456789ABCDEF"[v & 15];
        }
        ihex[64] = 0;
        klog_fmt("CRASHSITE insn_bytes=%s", ihex);
        // Dump stack to find caller chain (10 dwords)
        for (int i = 0; i < 10; i++) {
            U32 addr = esp + (U32)(i * 4);
            U32 val = cpu->memory->readd(addr);
            klog_fmt("CRASHSITE stack+%d addr=%08X val=%08X", i*4, addr, val);
        }
        fired = true;
    }
    if (!(eip == 0x1064D3F8 && esi == 0)) {
        fired = false;
    }
}

#if defined(BOXEDWINE_DIRECT_NORMAL_DISPATCH) || defined(BOXEDWINE_WASM_JIT)
static void OPCALL normalDispatch(CPU* cpu, DecodedOp* op) {
#ifdef BW64_INST_TRACE
    // Guest-memory diagnostic traps belong only in instruction-trace builds.
    // In particular, the PEB watcher caches an address from one process that
    // need not be mapped in the next process dispatched by this interpreter.
    // 32-bit ntdll_wcsnicmp entry trap (2026-10-09): always active, cheap EIP check.
    // Catches NULL env with size>0 that leads to 0xD3F8 crash.
    if (cpu->eip.u32 == 0x1064D3B0) {
        bw64CrashTrap(cpu);
    }
    // 32-bit PEB corruption watcher (2026-10-09): catches Environment=0x31 when written.
    bw64PebWatch(cpu);
    // 32-bit crash-site trap (2026-10-09): catches ESI=0 at 0x1064D3F8.
    bw64CrashSiteTrap(cpu);
    bw64TraceRecord(cpu, op);
#endif
    switch (op->inst) {
#undef INIT_CPU
#define INIT_CPU(e, f) case e: MUSTTAIL return normal_##f(cpu, op);
#include "../common/cpu_init.h"
#include "../common/cpu_init_mmx.h"
#include "../common/cpu_init_sse.h"
#include "../common/cpu_init_sse2.h"
#include "../common/cpu_init_fpu.h"
#undef INIT_CPU
#ifdef BOXEDWINE_MULTI_THREADED
#define INIT_CPU_LOCK(e, f) case e##_Lock: MUSTTAIL return normal_##f##_lock(cpu, op);
#include "../common/cpu_init_lock.h"
#undef INIT_CPU_LOCK
#endif
        case SIDT:     MUSTTAIL return normal_sidt(cpu, op);
        case Callback: MUSTTAIL return onExitSignal(cpu, op);
        case TestEnd:  MUSTTAIL return onTestEnd(cpu, op);
        default:
            // WASM safety: Callback ops must never use pfn (raw address -> invalid table index)
            if (op->inst == Callback) MUSTTAIL return onExitSignal(cpu, op);
            MUSTTAIL return op->pfn(cpu, op); // unimplemented/invalid opcodes
    }
}
#endif

static void initNormalOps() {
    if (normalOpsInitialized)
        return;
    normalOpsInitialized = 1;
    for (int i=0;i<InstructionCount;i++) {
        normalOps[i] = normal_invalid;
    }
#define INIT_CPU(e, f) normalOps[e] = normal_##f;
#include "../common/cpu_init.h"
#include "../common/cpu_init_mmx.h"
#include "../common/cpu_init_sse.h"
#include "../common/cpu_init_sse2.h"
#include "../common/cpu_init_fpu.h"
#ifdef BOXEDWINE_MULTI_THREADED
#define INIT_CPU_LOCK(e, f) normalOps[e##_Lock] = normal_##f##_lock;
#include "../common/cpu_init_lock.h"
#undef INIT_CPU_LOCK
#endif
#undef INIT_CPU

    normalOps[SLDTReg] = nullptr;
    normalOps[SLDTE16] = nullptr;
    normalOps[STRReg] = nullptr;
    normalOps[STRE16] = nullptr;
    normalOps[LLDTR16] = nullptr;
    normalOps[LLDTE16] = nullptr;
    normalOps[LTRR16] = nullptr;
    normalOps[LTRE16] = nullptr;
    normalOps[VERRR16] = nullptr;
    normalOps[VERWR16] = nullptr;
    normalOps[SGDT] = nullptr;
    normalOps[SIDT] = normal_sidt;
    normalOps[LGDT] = nullptr;
    normalOps[LIDT] = nullptr;
    normalOps[SMSWRreg] = nullptr;
    normalOps[SMSW] = nullptr;
    normalOps[LMSWRreg] = nullptr;
    normalOps[LMSW] = nullptr;
    normalOps[INVLPG] = nullptr;
    normalOps[Callback] = onExitSignal;
    normalOps[TestEnd] = onTestEnd;
}

#if defined(BOXEDWINE_JIT)
void OPCALL firstDynamicOp(CPU* cpu, DecodedOp* op);
#endif

NormalCPU::NormalCPU(KMemory* memory) : CPU(memory) {
    initNormalOps();
#ifdef BOXEDWINE_JIT
    this->firstOp = firstDynamicOp;
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(memory->mutex);
    memcpy(this->calculateCF, memory->process->calculateCF, sizeof(calculateCF));
#else
    this->firstOp = nullptr;
#endif
}

OpCallback NormalCPU::getFunctionForOp(DecodedOp* op) {
    return normalOps[op->inst];
}

bool NormalCPU::isValidExecutableAddress(U32 address) {
    return (memory->getPageFlags(address >> K_PAGE_SHIFT) & PAGE_EXEC) != 0;
}

DecodedOp* NormalCPU::getOp(U32 startIp, U32 jumpTargetFlags) {
    if (!this->thread->process) // exit was called, don't need to pre-cache the next block
        return nullptr;

    DecodedOp* op = memory->getDecodedOp(startIp);

    if (!op) {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(memory->mutex);
        op = memory->getDecodedOp(startIp);
        if (!op) {
            U32 opCount = 0;
            U32 eipLen = 0;
            U32 address = startIp;

            op = decodeBlock(this, startIp, this->isBig(), opCount, eipLen);
            if (!op) {
                return nullptr;
            }
            DecodedOp* nextOp = op;

            while (nextOp) {
                if (!nextOp->pfn) {
                    nextOp->pfn = getFunctionForOp(nextOp);
                }
                if (memory->isAddressDynamic(address, nextOp->len)) {
                    nextOp->flags |= OP_FLAG_NO_JIT;
#ifdef BOXEDWINE_JIT
                    nextOp->runCount = JIT_RUN_COUNT + 1;
#endif
                }
                address += nextOp->len;
                nextOp = nextOp->next;
            }
            this->thread->memory->addCode_nolock(startIp, eipLen, op, opCount);
        }
    }
#ifdef BOXEDWINE_JIT
    op->flags2 |= jumpTargetFlags;
    if (jumpTargetFlags && (op->flags2 & OP_FLAG2_JUMP_TARGET_ASSUMED_FALSE)) {
        kpanic("JUMP_TARGET_ASSUMED_FALSE");
    }
#endif
    return op;
}

void NormalCPU::run() {
#if defined(BOXEDWINE_WASM_JIT) && !defined(__TEST)
    if (nextOp) {
        // WASM JIT fast exits can carry DecodedOp* values across code-cache
        // invalidation; refetch from the live cache before dereferencing.
        DecodedOp* liveOp = memory->getDecodedOp(getEipAddress());
        if (liveOp != nextOp) {
            nextOp = liveOp;
        }
    }
#endif
    if (!nextOp) {
        if (thread->terminating) {
            return;
        }
        nextOp = getNextOp();
        if (!nextOp) {
            thread->seg_mapper(getEipAddress(), true, false, false);
            nextOp = getNextOp();
            if (!nextOp) {
                kpanic_fmt("Failed to get op for thread %d of process %d at address %x", thread->id, thread->process->id, getEipAddress());
            }
        }
    }
#ifdef BOXEDWINE_DIRECT_NORMAL_DISPATCH
    branchBudget = 64;
    normalDispatch(this, nextOp);
#else
#ifdef BOXEDWINE_JIT
    if (nextOp->runCount <= JIT_RUN_COUNT || !calculateCF[0]) {
        firstOp(this, nextOp);
    } else {
        if (nextOp->inst == Callback) onExitSignal(this, nextOp); else nextOp->pfn(this, nextOp);
#if !defined(BOXEDWINE_MULTI_THREADED)
        this->blockInstructionCount += nextOp->blockOpCount;
#endif
    }
#else
    if (nextOp->inst == Callback) onExitSignal(this, nextOp); else nextOp->pfn(this, nextOp);
#endif
#endif
}
