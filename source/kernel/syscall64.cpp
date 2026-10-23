/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"

#ifdef BOXEDWINE_GUEST_X64

#include "syscall64.h"
#include "cpu64.h"
#include "kmemory64.h"
#include "kevent.h"
#include "ksocket.h"
#include <thread>   // std::this_thread::yield() for sched_yield
#include <mutex>    // std::recursive_mutex for BW64_SERIAL_TEARDOWN
#include <unordered_map>
#include <unordered_set>
#include "kunixsocket.h"
#include "kepoll.h"
#include "kpoll.h"
#include "ripSampler.h"
#include "guestprofiler.h"
#include "bw64metrics.h"   // live frame-stage metrics (BW64_METRICS, default on)
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#ifdef BOXEDWINE_OPENGL
#include "../opengl/gl64bridge.h"
#include "../opengl/gl64bridge_abi.h"
#endif
#ifdef BOXEDWINE_VULKAN64
#include "../vulkan/vk64bridge.h"
#include "../vulkan/vk64bridge_abi.h"
#endif

// x86-64 Linux syscall numbers used here. The canonical table lives in
// arch/x86/entry/syscalls/syscall_64.tbl in the Linux source; the values
// below are stable Linux ABI and never change.
#define X64_SYS_read              0
#define X64_SYS_write             1
#define X64_SYS_open              2
#define X64_SYS_close             3
#define X64_SYS_stat              4
#define X64_SYS_fstat             5
#define X64_SYS_lstat             6
#define X64_SYS_poll              7
#define X64_SYS_ppoll             271
#define X64_SYS_pselect6          270
#define X64_SYS_lseek             8
#define X64_SYS_mmap              9
#define X64_SYS_mprotect          10
#define X64_SYS_munmap            11
#define X64_SYS_mincore           27
#define X64_SYS_sync_file_range   277
#define X64_SYS_mbind             237
#define X64_SYS_get_mempolicy     239
#define X64_SYS_brk               12
#define X64_SYS_shmget            29
#define X64_SYS_shmat             30
#define X64_SYS_shmctl            31
#define X64_SYS_shmdt             67
#define X64_SYS_rt_sigaction      13
#define X64_SYS_rt_sigprocmask    14
#define X64_SYS_ptrace            101
#define X64_SYS_ioctl             16
#define X64_SYS_pread64           17
#define X64_SYS_writev            20
#define X64_SYS_access            21
#define X64_SYS_readlink          89
#define X64_SYS_readlinkat        267
#define X64_SYS_faccessat         269
#define X64_SYS_faccessat2        439
#define X64_SYS_mkdir             83
#define X64_SYS_mkdirat           258
#define X64_SYS_symlink           88
#define X64_SYS_symlinkat         266
#define X64_SYS_link              86
#define X64_SYS_linkat            265
#define X64_SYS_utimensat         280
#define X64_SYS_socketpair        53
#define X64_SYS_shutdown          48
#define X64_SYS_socket            41
#define X64_SYS_connect           42
#define X64_SYS_accept            43
#define X64_SYS_bind              49
#define X64_SYS_listen            50
#define X64_SYS_getsockname       51
#define X64_SYS_getpeername       52
#define X64_SYS_setsockopt        54
#define X64_SYS_getsockopt        55
#define X64_SYS_sendmsg           46
#define X64_SYS_recvmsg           47
#define X64_SYS_sendmmsg          307
#define X64_SYS_recvmmsg          299
#define X64_SYS_sendto            44
#define X64_SYS_recvfrom          45
#define X64_SYS_unlink            87
#define X64_SYS_unlinkat          263
#define X64_SYS_rename            82
#define X64_SYS_renameat          264
#define X64_SYS_renameat2         316
#define X64_SYS_fchdir            81
#define X64_SYS_ftruncate         77
#define X64_SYS_umask             95
#define X64_SYS_setsid            112
#define X64_SYS_getpid            39
#define X64_SYS_exit              60
#define X64_SYS_uname             63
#define X64_SYS_setuid            105
#define X64_SYS_setgid            106
#define X64_SYS_getuid            102
#define X64_SYS_time              201
#define X64_SYS_getgid            104
#define X64_SYS_geteuid           107
#define X64_SYS_getegid           108
#define X64_SYS_prctl             157
#define X64_SYS_arch_prctl        158
#define X64_SYS_gettid            186
#define X64_SYS_futex             202
#define X64_SYS_set_tid_address   218
#define X64_SYS_clock_gettime     228
#define X64_SYS_exit_group        231
#define X64_SYS_openat            257
#define X64_SYS_newfstatat        262
#define X64_SYS_set_robust_list   273
#define X64_SYS_madvise           28
#define X64_SYS_fadvise64         221
#define X64_SYS_mremap            25
#define X64_SYS_sigaltstack       131
#define X64_SYS_rt_sigreturn      15
#define X64_SYS_dup               32
#define X64_SYS_dup2              33
#define X64_SYS_getcwd            79
#define X64_SYS_chdir             80
#define X64_SYS_fcntl             72
#define X64_SYS_pipe              22
#define X64_SYS_pipe2             293
#define X64_SYS_getdents64        217
#define X64_SYS_tgkill            234
#define X64_SYS_tkill             200
#define X64_SYS_prlimit64         302
#define X64_SYS_getrlimit         97
#define X64_SYS_setrlimit         160
#define X64_SYS_sched_getscheduler 145
#define X64_SYS_sched_setscheduler 144
#define X64_SYS_sched_getparam    143
#define X64_SYS_mlockall          151
#define X64_SYS_munlockall        152
#define X64_SYS_mlock             149
#define X64_SYS_munlock           150
#define X64_SYS_getrandom         318
#define X64_SYS_memfd_create      319
#define X64_SYS_getcpu            309
#define X64_SYS_process_vm_readv  310
#define X64_SYS_process_vm_writev 311
#define X64_SYS_membarrier        324
#define X64_SYS_timerfd_create    283
#define X64_SYS_timerfd_settime   286
#define X64_SYS_timerfd_gettime   287
#define X64_SYS_sched_yield       24
#define X64_SYS_sched_setaffinity 203
#define X64_SYS_sched_getaffinity 204
#define X64_SYS_statfs            137
#define X64_SYS_fstatfs           138
#define X64_SYS_kill              62
#define X64_SYS_gettimeofday      96
#define X64_SYS_getrusage         98
#define X64_SYS_sysinfo           99
#define X64_SYS_times             100
#define X64_SYS_getppid           110
#define X64_SYS_getpgrp           111
#define X64_SYS_getpgid           121
#define X64_SYS_getsid            124
#define X64_SYS_clock_getres      229
#define X64_SYS_clock_nanosleep   230
#define X64_SYS_nanosleep         35
#define X64_SYS_rseq              334
#define X64_SYS_clone3            435
#define X64_SYS_eventfd2          290
#define X64_SYS_epoll_create1     291
#define X64_SYS_epoll_create      213
#define X64_SYS_epoll_ctl         233
#define X64_SYS_epoll_wait        232
#define X64_SYS_pwrite64          18
#define X64_SYS_readv             19
#define X64_SYS_select            23
#define X64_SYS_chmod             90
#define X64_SYS_fchmod            91
#define X64_SYS_clone             56
#define X64_SYS_fork              57
#define X64_SYS_vfork             58
#define X64_SYS_execve            59
#define X64_SYS_wait4             61
#define X64_SYS_pause             34
#define X64_SYS_getitimer         36
#define X64_SYS_setitimer         38
#define X64_SYS_rt_sigpending     127
#define X64_SYS_rt_sigtimedwait   128
#define X64_SYS_rt_sigqueueinfo   129
#define X64_SYS_rt_sigsuspend     130

// arch_prctl subfunctions
#define X64_ARCH_SET_GS  0x1001
#define X64_ARCH_SET_FS  0x1002
#define X64_ARCH_GET_FS  0x1003
#define X64_ARCH_GET_GS  0x1004

// Linux returns errno as the negated value in RAX. We use the same K_*
// constants as the 32-bit path so error semantics stay aligned.
#ifndef K_ENOSYS
#define K_ENOSYS 38
#endif
#ifndef K_EINVAL
#define K_EINVAL 22
#endif
#ifndef K_EFAULT
#define K_EFAULT 14
#endif
#ifndef K_EAGAIN
#define K_EAGAIN 11
#endif
#ifndef K_ENOMEM
#define K_ENOMEM 12
#endif
#ifndef K_ECHILD
#define K_ECHILD 10
#endif
#ifndef K_EINTR
#define K_EINTR 4
#endif
#ifndef K_EBADF
#define K_EBADF 9
#endif
#ifndef K_EMFILE
#define K_EMFILE 24
#endif
#ifndef K_ESRCH
#define K_ESRCH 3
#endif
#ifndef K_ENODATA
#define K_ENODATA 61
#endif
#ifndef K_ENOTSUP
#define K_ENOTSUP 95
#endif

// FUTEX_* op codes from <linux/futex.h>. We handle WAIT/WAKE + their
// BITSET variants (glibc 2.35+ uses WAKE_BITSET for pthread_cond_signal)
// and recognize REQUEUE/CMP_REQUEUE/WAKE_OP so we don't reject them with
// EINVAL — they degrade to "no waiters woken" which is correct for our
// single-threaded world. The CLOCK_REALTIME bit (0x100) is ignored
// because we don't block anyway.
#define X64_FUTEX_WAIT             0
#define X64_FUTEX_WAKE             1
#define X64_FUTEX_REQUEUE          3
#define X64_FUTEX_CMP_REQUEUE      4
#define X64_FUTEX_WAKE_OP          5
#define X64_FUTEX_WAIT_BITSET      9
#define X64_FUTEX_WAKE_BITSET      10
#define X64_FUTEX_PRIVATE_FLAG     128
#define X64_FUTEX_CLOCK_REALTIME   256
#define X64_FUTEX_WAIT_PRIVATE     (X64_FUTEX_WAIT | X64_FUTEX_PRIVATE_FLAG)
#define X64_FUTEX_WAKE_PRIVATE     (X64_FUTEX_WAKE | X64_FUTEX_PRIVATE_FLAG)

// MAP_* bits used by mmap. Kept local to avoid pulling kernel.h here.
#ifndef K_MAP_ANONYMOUS
#define K_MAP_ANONYMOUS 0x20
#endif
#ifndef K_MAP_FIXED
#define K_MAP_FIXED 0x10
#endif

// Base address for mmap(NULL, ...) placements. High enough to never collide
// with PIE-loaded segments (X64_PIE_BASE=0x400000000) or the program break.
#define MMAP64_BASE 0x700000000ULL

// Address-space allocation for mmap(NULL,...) now lives in
// KMemory64::mmapReserveAndMap, which scans for a free gap and maps it as ONE
// atomic step under the process mmap lock. The old split here (allocMmapRange
// returns an address, the caller maps it afterwards) was a TOCTOU race in the
// multi-threaded build: two sibling threads scanned, both saw the same gap
// free, both mapped there, and the second map zeroed the first → guest-heap
// corruption ("malloc(): corrupted double linked list") in wineserver during
// the boot storm. The shared bump cursor moved to KMemory64::mmapNext.

// BW64_CRASHRING: a tiny lock-light ring of the last few socket writes (pid +
// fd + first 16 bytes = the wineserver request header / opcode). When a guest
// process prints an abort to stderr (e.g. wineserver's "release_object:
// Assertion 'obj->refcount' failed"), we dump the ring so the request sequence
// that led to the crash is visible WITHOUT the multi-GB BW64_IPCDUMP firehose.
// Zero cost when the env var is unset.
// kind: 'W'=write, 'R'=read, 'M'=recvmsg, 'F'=scm_rights fd delivered (recv),
//       'S'=sendmsg scm_rights fd sent (e0=#fds, e1=first guest fd).
// extra0/extra1 carry per-kind context (see crashRingRecordRead callers).
struct CrashRingEntry { char kind; U32 pid; U32 fd; U32 len; U32 extra0; U32 extra1; U8 hdr[16]; };
// Legacy serialization across file syscall paths. Each ZIP open now owns its
// decompression stream. Positioned reads also acquire KFile's position lock:
// lseek does not use this global mutex and must not interrupt seek/read/restore.
static std::mutex g_fileReadMutex;

static CrashRingEntry g_crashRing[64];
static std::atomic<U32> g_crashRingPos{0};
static bool g_crashRingOn = false;
static bool g_crashRingChecked = false;
// BW64_WSREAD: also record the READ side (read/recvmsg/scm) of wineserver
// sockets into the same ring, so the dump shows the request/reply/fd-pass
// sequence — needed to catch a duplicated or mis-framed request that drives
// wineserver's release_object double-free. Independent gate, zero cost when off.
static bool g_wsReadOn = false;
static bool g_wsReadChecked = false;
static bool wsReadEnabled() {
    if (!g_wsReadChecked) { g_wsReadOn = getenv("BW64_WSREAD") != nullptr; g_wsReadChecked = true; }
    return g_wsReadOn;
}
static void crashRingPut(char kind, U32 pid, U32 fd, const U8* data, U32 count, U32 extra0, U32 extra1) {
    U32 i = g_crashRingPos.fetch_add(1) % 64;
    g_crashRing[i].kind = kind;
    g_crashRing[i].pid = pid; g_crashRing[i].fd = fd; g_crashRing[i].len = count;
    g_crashRing[i].extra0 = extra0; g_crashRing[i].extra1 = extra1;
    U32 n = count < 16 ? count : 16;
    for (U32 j = 0; j < 16; j++) g_crashRing[i].hdr[j] = (data && j < n) ? data[j] : 0;
}
static void crashRingRecord(U32 pid, U32 fd, const U8* data, U32 count) {
    if (!g_crashRingChecked) { g_crashRingOn = getenv("BW64_CRASHRING") != nullptr; g_crashRingChecked = true; }
    if (!g_crashRingOn) return;
    crashRingPut('W', pid, fd, data, count, 0, 0);
}
// BW64_WSCONC: concurrency witness for the bug #2 teardown crash. wine's
// wineserver is designed single-threaded (one epoll loop processes every
// client request + disconnect strictly serially). If our MT model ever has two
// HOST threads inside wineserver's own request processing at the same instant,
// a close_handle's release_object can race the disconnect-cascade's release of
// the same object -> refcount underflow -> the object.c:443 assert. This counts
// how many host threads are concurrently reading wineserver's (pid 14) socket;
// any value >1 is the smoking gun. Keyed by the READING process id so it only
// arms for wineserver itself. Zero cost unless BW64_WSCONC is set.
static bool g_wsConcInit = false, g_wsConcOn = false;
static std::atomic<int> g_wsConcInflight{0};
static std::atomic<int> g_wsConcMax{0};
static bool wsConcEnabled() {
    if (!g_wsConcInit) { g_wsConcOn = getenv("BW64_WSCONC") != nullptr; g_wsConcInit = true; }
    return g_wsConcOn;
}
// Read-side recorder (used by sys_read64 / sys_recvmsg64 / the scm witness).
// Gated by BW64_WSREAD AND the same ring being on so the dump path fires.
static void crashRingRecordRead(char kind, U32 pid, U32 fd, const U8* data, U32 count, U32 extra0, U32 extra1) {
    if (!g_crashRingChecked) { g_crashRingOn = getenv("BW64_CRASHRING") != nullptr; g_crashRingChecked = true; }
    if (!g_crashRingOn || !wsReadEnabled()) return;
    crashRingPut(kind, pid, fd, data, count, extra0, extra1);
}
static void crashRingDump(const char* why) {
    if (!g_crashRingOn) return;
    klog_fmt("CRASHRING dump (%s) — last 64 socket events, oldest first:", why);
    U32 start = g_crashRingPos.load();
    for (U32 k = 0; k < 64; k++) {
        CrashRingEntry& e = g_crashRing[(start + k) % 64];
        if (e.kind == 0) continue;
        klog_fmt("  CR[%u] %c pid=%u fd=%u len=%u e0=%u e1=%u  [%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x]",
                 k, e.kind, e.pid, e.fd, e.len, e.extra0, e.extra1,
                 e.hdr[0],e.hdr[1],e.hdr[2],e.hdr[3],e.hdr[4],e.hdr[5],e.hdr[6],e.hdr[7],
                 e.hdr[8],e.hdr[9],e.hdr[10],e.hdr[11],e.hdr[12],e.hdr[13],e.hdr[14],e.hdr[15]);
    }
}

// BW64_RIPSAMPLE module registration for PE DLLs.
//
// Modern wine ships its DLLs (wined3d.dll, d3d9.dll, ...) as native PE32+ files,
// NOT winelib .so halves. wine's PE loader does NOT mmap those section bodies
// from an fd — it `read()`/`pread()`s an entire section in one call into an
// ANONYMOUS guest mapping (see the long comments in sys_read64/sys_pread64 about
// "wine's PE loader reads whole multi-MB sections in one read()"). Because that
// path never touches sys_mmap64_file, the PE image range is never handed to
// ripSamplerNoteModule — so a sampled RIP inside wined3d.dll resolves to nothing
// and any stack backtrace through it is blank. (The fd-mmap registration at
// sys_mmap64_file only covers ELF .so DLLs and a PE's *header* page.)
//
// Fix: whenever a read/pread fills guest memory FROM a file whose path ends in a
// PE image extension, register the destination range [dst, dst+len) under that
// file's basename. Each section read registers its own slice; the newest-wins
// resolver in ripSampler stitches them so an address in any section resolves to
// "wined3d.dll+0xNNN". Gated by the optional RIP/guest profilers.
static void ripSamplerNotePeRead(CPU64* cpu, const KFileDescriptorPtr& fdesc,
                                 U64 dst, U64 len) {
    const bool ripModules = ripSamplerModuleTrackingEnabled();
    const bool guestProfile = bwGuestProfileEnabled();
    if ((!ripModules && !guestProfile) || len == 0 || !cpu->thread || !cpu->thread->process) return;
    std::shared_ptr<KFile> kf = std::dynamic_pointer_cast<KFile>(fdesc->kobject);
    if (!kf || !kf->openFile || !kf->openFile->node) return;
    const BString& path = kf->openFile->node->path;
    // Match PE images by extension (case-insensitive): .dll / .exe.
    if (!path.endsWith(".dll", true) && !path.endsWith(".exe", true)) return;
    if (guestProfile) bwGuestProfileNoteModule((int)cpu->thread->process->id, dst, len, path.c_str());
    if (ripModules) ripSamplerNoteModule((int)cpu->thread->process->id, dst, len, path.c_str());
}

static U64 sys_write64(CPU64* cpu, U64 fd, U64 buf, U64 count) {
    if (count == 0) return 0;
    // Ceiling only (was a 1MB cap that silently truncated large writes for any
    // caller that doesn't loop on a short return). 256MB is plenty for the temp
    // buffer while still bounding a pathological count.
    if (count > (256ULL << 20)) count = 256ULL << 20;
    std::vector<U8> buffer((size_t)count + 1);
    cpu->memory->memcpyFromGuest(buffer.data(), buf, count);
    buffer[count] = 0;
    if (fd == 1 || fd == 2) {
        // Tee stdout/stderr to host console so ld-linux + glibc diagnostics
        // surface immediately. Also forward to the kobject so anything
        // tailing the host FS still sees it.
        U32 wpid = (cpu->thread && cpu->thread->process) ? cpu->thread->process->id : 0;
        const char* wexe = (cpu->thread && cpu->thread->process) ? cpu->thread->process->name.c_str() : "?";
        klog_fmt("[guest fd=%llu pid=%u %s] %s", (unsigned long long)fd, (unsigned)wpid, wexe, (const char*)buffer.data());
        // On a guest abort, dump the request ring so we can see the wineserver
        // request sequence that triggered the double-release.
        if (fd == 2 && (strstr((const char*)buffer.data(), "release_object") ||
                        strstr((const char*)buffer.data(), "Assertion") ||
                        strstr((const char*)buffer.data(), "tcache") ||
                        strstr((const char*)buffer.data(), "unaligned") ||
                        strstr((const char*)buffer.data(), "unimplemented function") ||
                        strstr((const char*)buffer.data(), "corrupted"))) {
            crashRingDump((const char*)buffer.data());
            // BW64_WSBT: at the assert WRITE (glibc prints the message BEFORE
            // abort()), wineserver's guest stack is still intact. Scan it for
            // code-looking return addresses so addr2line on the wineserver binary
            // can name the release_object CALLER (the buggy teardown path). The
            // assert chain is __assert_fail -> release_object -> <culprit>. We
            // print RIP + any stack word that lands in the wineserver text range
            // [base, base+size). The image base is logged once at load (BW64_WSBT
            // also enables that). Cheap: only on the abort path.
            if (getenv("BW64_WSBT")) {
                auto rdq = [&](U64 a) -> U64 {
                    U64 v = 0;
                    for (int b = 0; b < 8; b++) v |= ((U64)cpu->memory->readb(a + b)) << (b * 8);
                    return v;
                };
                klog_fmt("WSBT: assert in pid=%u %s  RIP=%llx RSP=%llx RBP=%llx",
                         (unsigned)wpid, wexe,
                         (unsigned long long)cpu->rip,
                         (unsigned long long)cpu->reg[X64_RSP].u64,
                         (unsigned long long)cpu->reg[X64_RBP].u64);
                U64 sp = cpu->reg[X64_RSP].u64;
                // Walk a wide stack window; print every word that could be a
                // return address into a mapped, executable-ish region. We don't
                // know the exact text range here, so print all plausible code
                // pointers (high-half canonical, page-aligned-ish) and let
                // post-processing filter by the wineserver mmap range.
                for (int i = 0; i < 160; i++) {
                    U64 v = rdq(sp + (U64)i * 8);
                    // Heuristic: wineserver/glibc code lives at low-ish 47-bit
                    // addresses; skip obvious data (small ints, ASCII, stack ptrs
                    // near sp). Print candidates with the 'CODE?' tag.
                    if (v > 0x10000 && v < 0x800000000000ULL &&
                        (v < sp - 0x100000 || v > sp + 0x100000)) {
                        klog_fmt("WSBT:   [rsp+%03x] = 0x%llx", i * 8, (unsigned long long)v);
                    }
                }
            }
            // BW64_MALLOCDUMP: for the glibc malloc-METADATA faces ("unaligned
            // tcache chunk" / "corrupted double-linked list" / "corrupted
            // unsorted chunks") — the dominant faces of bug #2 — REFWATCH can't
            // fire (no release_object). glibc's malloc_printerr(msg) is reached
            // from the freelist-walk that detected the bad chunk, and the bad
            // chunk pointer is usually still live in a callee-saved register
            // (rbx/rbp/r12-r15, preserved across __libc_message) or a nearby
            // stack slot. Dump them + flag stack words that point into the
            // wineserver heap range so we can correlate the corrupted address
            // against recent writes (BW64_MEMRING). Cheap: abort path only.
            if (getenv("BW64_MALLOCDUMP") &&
                (strstr((const char*)buffer.data(), "tcache") ||
                 strstr((const char*)buffer.data(), "corrupted") ||
                 strstr((const char*)buffer.data(), "unaligned"))) {
                auto rdq = [&](U64 a) -> U64 {
                    U64 v = 0;
                    for (int b = 0; b < 8; b++) v |= ((U64)cpu->memory->readb(a + b)) << (b * 8);
                    return v;
                };
                klog_fmt("MALLOCDUMP pid=%u %s RIP=%llx RSP=%llx RBP=%llx RBX=%llx R12=%llx R13=%llx R14=%llx R15=%llx RDI=%llx RSI=%llx",
                         (unsigned)wpid, wexe, (unsigned long long)cpu->rip,
                         (unsigned long long)cpu->reg[X64_RSP].u64, (unsigned long long)cpu->reg[X64_RBP].u64,
                         (unsigned long long)cpu->reg[X64_RBX].u64, (unsigned long long)cpu->reg[X64_R12].u64,
                         (unsigned long long)cpu->reg[X64_R13].u64, (unsigned long long)cpu->reg[X64_R14].u64,
                         (unsigned long long)cpu->reg[X64_R15].u64, (unsigned long long)cpu->reg[X64_RDI].u64,
                         (unsigned long long)cpu->reg[X64_RSI].u64);
                // Heap candidates: glibc's main arena heap lives well above the
                // PE/text mappings and below the stack. Print stack words that
                // look like heap data pointers (not text <0x500000000, not near
                // rsp), and dump 16 bytes around each — a corrupted chunk shows a
                // mis-aligned or wild fd/bk.
                U64 sp = cpu->reg[X64_RSP].u64;
                for (int i = 0; i < 64; i++) {
                    U64 v = rdq(sp + (U64)i * 8);
                    if (v > 0x500000000ULL && v < 0x800000000000ULL &&
                        (v < sp - 0x200000 || v > sp + 0x200000)) {
                        klog_fmt("MALLOCDUMP   [rsp+%03x]=0x%llx  chunk?[-8..+16]= %016llx | %016llx %016llx %016llx",
                                 i * 8, (unsigned long long)v,
                                 (unsigned long long)rdq(v - 8), (unsigned long long)rdq(v),
                                 (unsigned long long)rdq(v + 8), (unsigned long long)rdq(v + 16));
                    }
                }
                // Correlate against the wineserver write ring (BW64_MEMRING).
                kmemory64DumpMemRing(0);
            }
        }
        // BW64_STUBDUMP: when wine reports a call to an unimplemented function,
        // dump the live register/stack state so we can identify the stub and the
        // ABI register the dll/func name strings actually landed in. The abort
        // message itself loses the names (they print as ".garbage").
        if (fd == 2 && getenv("BW64_STUBDUMP") &&
            strstr((const char*)buffer.data(), "unimplemented function")) {
            auto rdStr = [&](U64 a) -> std::string {
                std::string s;
                if (!a) return "<null>";
                for (int i = 0; i < 64; i++) {
                    U8 c = cpu->memory->readb(a + i);
                    if (!c) break;
                    if (c < 32 || c > 126) { s += '?'; } else s += (char)c;
                }
                return s.empty() ? "<empty>" : s;
            };
            klog_fmt("STUBDUMP pid=%u RIP=%llx RSP=%llx RAX=%llx RCX=%llx RDX=%llx R8=%llx R9=%llx RDI=%llx RSI=%llx",
                     (unsigned)wpid, (unsigned long long)cpu->rip,
                     (unsigned long long)cpu->reg[X64_RSP].u64, (unsigned long long)cpu->reg[X64_RAX].u64,
                     (unsigned long long)cpu->reg[X64_RCX].u64, (unsigned long long)cpu->reg[X64_RDX].u64,
                     (unsigned long long)cpu->reg[X64_R8].u64, (unsigned long long)cpu->reg[X64_R9].u64,
                     (unsigned long long)cpu->reg[X64_RDI].u64, (unsigned long long)cpu->reg[X64_RSI].u64);
            klog_fmt("STUBDUMP  RCX->\"%s\" RDX->\"%s\" R8->\"%s\" RDI->\"%s\" RSI->\"%s\"",
                     rdStr(cpu->reg[X64_RCX].u64).c_str(), rdStr(cpu->reg[X64_RDX].u64).c_str(),
                     rdStr(cpu->reg[X64_R8].u64).c_str(), rdStr(cpu->reg[X64_RDI].u64).c_str(),
                     rdStr(cpu->reg[X64_RSI].u64).c_str());
            auto rdq = [&](U64 a) -> U64 {
                U64 v = 0;
                for (int b = 0; b < 8; b++) v |= ((U64)cpu->memory->readb(a + b)) << (b * 8);
                return v;
            };
            U64 sp = cpu->reg[X64_RSP].u64;
            for (int i = 0; i < 12; i++) {
                U64 v = rdq(sp + (U64)i * 8);
                klog_fmt("STUBDUMP  [rsp+%02x]=%llx  ->\"%s\"", i * 8, (unsigned long long)v, rdStr(v).c_str());
            }
        }
    } else if (cpu->thread && cpu->thread->process) {
        crashRingRecord(cpu->thread->process->id, (U32)fd, buffer.data(), (U32)count);
    }
    if (!cpu->thread || !cpu->thread->process) {
        return count;
    }
    KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)fd);
    if (!fdesc) {
        if (fd == 1 || fd == 2) return count; // already klog'd
        return (U64)-9; // -EBADF
    }
    if (!fdesc->canWrite()) {
        return (U64)-K_EINVAL;
    }
    U32 wrote = fdesc->kobject->writeNative(buffer.data(), (U32)count);
    // A connected stream socket whose peer object is TRANSIENTLY unreferenced
    // under boot-time fd-table churn returns -K_EWOULDBLOCK (see
    // KUnixSocketObject::writeNative). On a blocking fd this must not surface to
    // the guest (wine treats a short/odd write to its wineserver request socket
    // as fatal -> "partial write"). Retry a bounded number of host yields so the
    // peer's strong ref reappears; only give up (real -EPIPE) if it never does.
    if ((S32)wrote == -K_EWOULDBLOCK && fdesc->kobject->isBlocking()) {
        for (int spin = 0; spin < 1000 && (S32)wrote == -K_EWOULDBLOCK; spin++) {
            std::this_thread::yield();
            wrote = fdesc->kobject->writeNative(buffer.data(), (U32)count);
        }
        if ((S32)wrote == -K_EWOULDBLOCK) {
            wrote = (U32)-K_EPIPE; // peer truly gone after retries
        }
    }
    if (getenv("BW64_IPCDUMP")) {
        char hex[64] = {0}; int n = (int)((count < 16) ? count : 16);
        for (int i = 0; i < n; i++) snprintf(hex + i*3, 4, "%02x ", buffer[i]);
        klog_fmt("IPC [pid=%d] write(fd=%d,count=%llu) -> %d  [%s]",
                 (int)cpu->thread->process->id, (int)fd,
                 (unsigned long long)count, (int)(S32)wrote, hex);
    }
    return (S32)wrote < 0 ? (U64)(S64)(S32)wrote : (U64)wrote;
}

static U64 sys_arch_prctl64(CPU64* cpu, U64 code, U64 addr) {
    switch (code) {
        case X64_ARCH_SET_FS:
            cpu->fsbase = addr;
            return 0;
        case X64_ARCH_SET_GS:
            cpu->gsbase = addr;
            return 0;
        case X64_ARCH_GET_FS:
            cpu->memory->writeq(addr, cpu->fsbase);
            return 0;
        case X64_ARCH_GET_GS:
            cpu->memory->writeq(addr, cpu->gsbase);
            return 0;
        default:
            return (U64)-K_EINVAL;
    }
}

// brk(0) → returns current break. brk(new) → tries to set break, returns
// new break on success or old break on failure (Linux contract). Stored
// per-process on KProcess::brkEnd in the 32-bit path; we put it on the
// thread's process the same way.
static U64 sys_brk64(CPU64* cpu, U64 newBrk) {
    // Resolve where the current break lives. Full kernel: KProcess::brkEnd64.
    // Standalone runner (no process): CPU64::runnerBrk, seeded by the runner
    // to just past the loaded image. If neither is available there's no heap
    // to manage, so report 0 (the historical no-process behaviour).
    bool hasProcess = (cpu->thread && cpu->thread->process);
    U64 oldBrk;
    if (hasProcess) {
        oldBrk = cpu->thread->process->brkEnd64;
    } else if (cpu->runnerBrk) {
        oldBrk = cpu->runnerBrk;
    } else {
        return 0;
    }

    if (newBrk == 0 || newBrk < oldBrk) {
        return oldBrk;
    }
    // Map the gap as anonymous RW. mmapAnonymousFixed expects page-aligned
    // addresses; round oldBrk up and newBrk up to page boundaries.
    U64 alignedOld = (oldBrk + 0xFFF) & ~0xFFFULL;
    U64 alignedNew = (newBrk + 0xFFF) & ~0xFFFULL;
    if (alignedNew > alignedOld) {
        U64 ret = cpu->memory->mmapAnonymousFixed(alignedOld, alignedNew - alignedOld, 0x3); // PROT_READ|WRITE
        if ((S64)ret < 0) {
            return oldBrk;
        }
    }
    if (hasProcess) cpu->thread->process->brkEnd64 = newBrk;
    else            cpu->runnerBrk = newBrk;
    return newBrk;
}

// Forward decl for the file-backed path; defined below.
static U64 sys_mmap64_file(CPU64* cpu, U64 addr, U64 length, U64 prot,
                           U64 flags, U64 fd, U64 offset);

static U64 sys_mmap64(CPU64* cpu, U64 addr, U64 length, U64 prot, U64 flags, U64 fd, U64 offset) {
    if (!(flags & K_MAP_ANONYMOUS)) {
        return sys_mmap64_file(cpu, addr, length, prot, flags, fd, offset);
    }
    if (getenv("BW64_MMAPDUMP") && length >= 0x1000000) {
        klog_fmt("MMAP [pid=%d] BIG anon enter addr=0x%llx len=0x%llx prot=0x%x",
                 (int)(cpu->thread ? cpu->thread->process->id : -1),
                 (unsigned long long)addr, (unsigned long long)length, (unsigned)prot);
    }
    U64 ret;
    bool fixed = (flags & K_MAP_FIXED) != 0;
    if (addr != 0 && fixed) {
        // MAP_FIXED: caller demands this exact address and expects any existing
        // mapping there to be replaced. Honor it verbatim.
        ret = cpu->memory->mmapAnonymousFixed(addr & ~0xFFFULL, length, (U32)prot);
    } else if (addr != 0) {
        // addr is a HINT (no MAP_FIXED). Linux may place elsewhere if the hint
        // is already occupied — and it MUST, because force-mapping over an
        // existing region silently destroys whatever lived there. Wine's view
        // manager then later MAP_FIXED-maps its own view at an address it still
        // believes is free, finds our stray anonymous mapping, and aborts in
        // create_view (`assert(view->protect & VPROT_SYSTEM)`, virtual.c:1578).
        // So: take the hint only if the whole range is free; otherwise let the
        // allocator pick a free range (kernel's prerogative for a hint).
        //
        // "Free" here means no ACCESSIBLE mapping — a page with any of R/W/X.
        // Crucially it does NOT mean "no K64_PAGE_MAPPED": wine reserves huge
        // PROT_NONE arenas (MAPPED but prot 0) and then mmaps committed pages at
        // HINT addresses *inside* its own reservation. Treating those reserved
        // pages as occupied would bounce the allocation to an unrelated address
        // and desync wine's view tree (hang/abort). Only a real R/W/X mapping is
        // a genuine conflict that forces relocation.
        U64 alignedAddr = addr & ~0xFFFULL;
        U64 pageStart = alignedAddr >> 12;
        U64 pageCount = (length + 0xFFFULL) >> 12;
        const U32 accessible = K64_PAGE_READ | K64_PAGE_WRITE | K64_PAGE_EXEC;
        bool rangeFree = true;
        for (U64 i = 0; i < pageCount; i++) {
            if (cpu->memory->getPageFlags(pageStart + i) & accessible) { rangeFree = false; break; }
        }
        if (rangeFree) {
            ret = cpu->memory->mmapAnonymousFixed(alignedAddr, length, (U32)prot);
        } else {
            ret = cpu->memory->mmapReserveAndMap(length, (U32)prot);
        }
    } else {
        // addr == 0: atomically reserve+map a free range. mmapReserveAndMap holds
        // the process mmap lock across the gap scan AND the map, so two sibling
        // threads can't be handed the same address (the old allocMmapRange-then-
        // map split was a TOCTOU race → guest-heap corruption in wineserver
        // during the MT boot storm).
        ret = cpu->memory->mmapReserveAndMap(length, (U32)prot);
    }
    if (getenv("BW64_MMAPDUMP") && length >= 0x1000000) {
        klog_fmt("MMAP [pid=%d] BIG anon exit  -> 0x%llx",
                 (int)(cpu->thread ? cpu->thread->process->id : -1),
                 (unsigned long long)ret);
    }
    (void)offset;
    return ret;
}

// ---- SysV shared memory (IPC_PRIVATE only) for the 64-bit guest ----
// Wine's unix-side ntdll uses anonymous shm segments to back memory views; the
// Save/Save As common dialog is the first GUI path that hits this. We service
// shmget/shmat/shmdt/shmctl out of the per-process shm64 table (see kprocess.h
// for why per-process is correct). shmat backs the segment with real anonymous
// pages via the normal mmap allocator, so wine sees a properly-mapped view and
// no longer trips create_view. Non-IPC_PRIVATE keys are rejected (-ENOSYS-free
// fallback isn't what wine wants here; it asks for IPC_PRIVATE).
#define K_IPC_PRIVATE   0
#define K_IPC_RMID      0
#define K_IPC_STAT      2
#define K_IPC_64        0x0100
#define K_SHM_RDONLY    010000

static U64 sys_shmget64(CPU64* cpu, U64 key, U64 size, U64 /*shmflg*/) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_EINVAL;
    // Only anonymous segments are supported (what wine requests). A named key
    // would need a system-wide table; wine's view-backing path never uses one.
    if (key != K_IPC_PRIVATE) return (U64)-K_EINVAL;
    KProcess* process = cpu->thread->process.get();
    S32 id = process->nextShm64Id++;
    KProcess::ShmSeg64 seg;
    seg.size = size;
    process->shm64[id] = seg;
    return (U64)(S64)id;
}

static U64 sys_shmat64(CPU64* cpu, U64 shmid, U64 shmaddr, U64 shmflg) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_EINVAL;
    KProcess* process = cpu->thread->process.get();
    auto it = process->shm64.find((S32)shmid);
    if (it == process->shm64.end()) return (U64)-K_EINVAL;
    KProcess::ShmSeg64& seg = it->second;
    U32 prot = (shmflg & K_SHM_RDONLY) ? K_PROT_READ : (K_PROT_READ | K_PROT_WRITE);
    U64 addr;
    if (shmaddr == 0) {
        addr = cpu->memory->mmapReserveAndMap(seg.size, prot);
    } else {
        addr = cpu->memory->mmapAnonymousFixed(shmaddr & ~0xFFFULL, seg.size, prot);
    }
    if ((S64)addr < 0) return addr;          // propagate -errno
    seg.address = addr;
    return addr;
}

static U64 sys_shmdt64(CPU64* cpu, U64 shmaddr) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_EINVAL;
    KProcess* process = cpu->thread->process.get();
    for (auto it = process->shm64.begin(); it != process->shm64.end(); ++it) {
        KProcess::ShmSeg64& seg = it->second;
        if (seg.address && seg.address == shmaddr) {
            cpu->memory->munmap(seg.address, seg.size);
            seg.address = 0;
            // A segment detached after IPC_RMID is destroyed (Linux semantics).
            if (seg.markedForDelete) process->shm64.erase(it);
            return 0;
        }
    }
    return (U64)-K_EINVAL;
}

static U64 sys_shmctl64(CPU64* cpu, U64 shmid, U64 cmd, U64 buf) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_EINVAL;
    KProcess* process = cpu->thread->process.get();
    cmd &= ~K_IPC_64;
    auto it = process->shm64.find((S32)shmid);
    if (it == process->shm64.end()) return (U64)-K_EINVAL;
    KProcess::ShmSeg64& seg = it->second;
    if (cmd == K_IPC_RMID) {
        // Mark for delete; reap now if nothing is attached (matches Linux: RMID
        // on an unattached segment frees it immediately).
        seg.markedForDelete = true;
        if (seg.address == 0) process->shm64.erase(it);
        return 0;
    }
    if (cmd == K_IPC_STAT) {
        if (!buf) return (U64)-K_EFAULT;
        // Minimal struct shmid64_ds: zero it, then fill shm_segsz. Wine's view
        // backing doesn't inspect the rest; zeroing keys/perms is safe.
        cpu->memory->memsetGuest(buf, 0, 112);
        cpu->memory->writeq(buf + 40, seg.size);   // shm_segsz
        return 0;
    }
    return (U64)-K_EINVAL;
}

// exit(2) — terminate the calling thread. If it's the last thread in the
// process, this ends the process (exitgroup). `group` forces process-wide
// termination regardless of thread count (exit_group(2)).
// BW64_SERIAL_TEARDOWN experiment: a global lock serializing the EXECUTION of
// 64-bit process/thread teardown. On real Linux wineserver observes every client
// disconnect + final request through ONE epoll, fully serial. Here the boot
// helpers run on separate HOST threads and exit truly concurrently, so their
// terminate_thread/close requests + socket EOFs interleave at instruction
// granularity — the suspected trigger for wineserver's release_object refcount
// underflow / heap corruption during the post-boot teardown storm. Holding this
// across the whole cleanup (which closes the wineserver socket -> EOF) forces
// wineserver to see one client's full disconnect before the next begins.
// Recursive so an exitgroup that internally drives sibling teardown can't
// self-deadlock. Env-gated so it's a clean A/B with no cost when off.
static std::recursive_mutex g_serialTeardownMutex;
static bool g_serialTeardownInit = false, g_serialTeardownOn = false;
static U64 sys_exit64(CPU64* cpu, U64 status, bool group) {
    if (!g_serialTeardownInit) {
        g_serialTeardownOn = std::getenv("BW64_SERIAL_TEARDOWN") != nullptr;
        g_serialTeardownInit = true;
    }
    if (cpu->thread && cpu->thread->process) {
        KProcess* p = cpu->thread->process.get();
        klog_fmt("CPU64: %s syscall, status=%lld  pid=%d exe='%s' cmd='%s'",
                 group ? "exit_group" : "exit", (long long)status,
                 (int)p->id, p->exe.c_str(), p->commandLine.c_str());
    } else {
        klog_fmt("CPU64: %s syscall, status=%lld",
                 group ? "exit_group" : "exit", (long long)status);
    }
    // BW64_EXIT_TRACE (D3D11 multi-device clean-exit(0) investigation):
    // On process exit, dump guest RIP + stack return addresses and resolve
    // them against Wine's PEB loader list, so we can see which guest code
    // called exit_group. Process exits are rare; zero steady-state cost.
    if (group && cpu->thread && cpu->thread->process) {
        U64 ripTrace = cpu->rip;
        U64 rspTrace = cpu->reg[X64_RSP].u64;
        U64 raTrace[4];
        for (int rti = 0; rti < 4; rti++)
            raTrace[rti] = cpu->memory->readq(rspTrace + (U64)rti * 8);
        klog_fmt("CPU64: exit_group trace tid=%d rip=0x%llx rsp=0x%llx ra=[0x%llx,0x%llx,0x%llx,0x%llx]",
                 (int)cpu->thread->id,
                 (unsigned long long)ripTrace, (unsigned long long)rspTrace,
                 (unsigned long long)raTrace[0], (unsigned long long)raTrace[1],
                 (unsigned long long)raTrace[2], (unsigned long long)raTrace[3]);
        U64 gsTrace = cpu->gsbase;
        if (gsTrace) {
            U64 pebTrace = cpu->memory->readq(gsTrace + 0x60);
            U64 ldrTrace = pebTrace ? cpu->memory->readq(pebTrace + 0x18) : 0;
            if (ldrTrace) {
                struct ExitMod { U64 base; char name[64]; };
                ExitMod modsTrace[48]; int nModsTrace = 0;
                // PEB_LDR_DATA+0x10=InLoadOrderModuleList; entries link via
                // InLoadOrderLinks at +0x00, so entry=link (no subtract).
                U64 headTrace = ldrTrace + 0x10;
                U64 linkTrace = cpu->memory->readq(headTrace);
                for (int mi = 0; mi < 64 && linkTrace && linkTrace != headTrace && nModsTrace < 48; mi++) {
                    U64 entryTrace = linkTrace;
                    U64 baseTrace = cpu->memory->readq(entryTrace + 0x30);
                    ExitMod mdTrace; mdTrace.base = baseTrace; mdTrace.name[0] = 0;
                    if (baseTrace) {
                        U16 nlenTrace = cpu->memory->readw(entryTrace + 0x58);
                        U64 nbufTrace = cpu->memory->readq(entryTrace + 0x60);
                        if (nlenTrace && nbufTrace && nlenTrace < 120) {
                            int cnTrace = nlenTrace / 2; if (cnTrace > 62) cnTrace = 62;
                            for (int ci = 0; ci < cnTrace; ci++) {
                                U16 chTrace = cpu->memory->readw(nbufTrace + (U64)ci * 2);
                                mdTrace.name[ci] = (chTrace < 128) ? (char)chTrace : '?';
                            }
                            mdTrace.name[cnTrace] = 0;
                        }
                        modsTrace[nModsTrace++] = mdTrace;
                    }
                    linkTrace = cpu->memory->readq(linkTrace);
                }
                U64 addrsTrace[5] = { ripTrace, raTrace[0], raTrace[1], raTrace[2], raTrace[3] };
                const char* tagsTrace[5] = { "rip", "ra0", "ra1", "ra2", "ra3" };
                for (int ai = 0; ai < 5; ai++) {
                    const char* mnTrace = "?"; U64 moTrace = addrsTrace[ai];
                    for (int i = 0; i < nModsTrace; i++) {
                        if (modsTrace[i].base && modsTrace[i].base <= addrsTrace[ai] &&
                            addrsTrace[ai] - modsTrace[i].base < moTrace) {
                            moTrace = addrsTrace[ai] - modsTrace[i].base;
                            mnTrace = modsTrace[i].name[0] ? modsTrace[i].name : "?";
                        }
                    }
                    klog_fmt("CPU64: exit_group %s=0x%llx -> %s+0x%llx", tagsTrace[ai],
                             (unsigned long long)addrsTrace[ai], mnTrace,
                             (unsigned long long)moTrace);
                }
            }
        }
    }
    // Stop this thread's run loop now, whatever else happens below.
    cpu->yield = true;
    if (cpu->thread && cpu->thread->process) {
        KProcess* process = cpu->thread->process.get();
        std::unique_lock<std::recursive_mutex> serialLk;
        if (g_serialTeardownOn) {
            serialLk = std::unique_lock<std::recursive_mutex>(g_serialTeardownMutex);
        }
        if (group) {
            process->exitgroup(cpu->thread, (U32)status);
        } else {
            // KProcess::exit requests termination. The MT executor cleans up
            // and wakes joiners after leaving its run loop; the final thread
            // also publishes process termination through exitgroup.
            process->exit(cpu->thread, (U32)status);
        }
    }
    return 0;
}

// struct utsname is 6 × 65-byte fixed strings on x86-64 Linux (390 bytes).
static U64 sys_uname64(CPU64* cpu, U64 bufAddr) {
    if (!bufAddr) return (U64)-K_EFAULT;
    char buf[6 * 65];
    std::memset(buf, 0, sizeof(buf));
    auto setField = [&](int idx, const char* s) {
        std::strncpy(buf + idx * 65, s, 64);
    };
    setField(0, "Linux");                  // sysname
    setField(1, "boxedwine64");            // nodename
    setField(2, "6.1.0-boxedwine");        // release — pretend modern kernel
    setField(3, "#1 SMP boxedwine64");     // version
    setField(4, "x86_64");                 // machine
    setField(5, "(none)");                 // domainname
    cpu->memory->memcpyToGuest(bufAddr, buf, sizeof(buf));
    return 0;
}

// getrandom — fill buffer with pseudo-random bytes. Good enough for ld.so's
// stack canary; not cryptographically strong, but glibc only needs entropy.
static U64 sys_getrandom64(CPU64* cpu, U64 bufAddr, U64 buflen, U64 /*flags*/) {
    if (!bufAddr || buflen == 0) return 0;
    if (buflen > 256) buflen = 256;
    U8 tmp[256];
    static U64 seed = 0x9E3779B97F4A7C15ULL;
    for (U64 i = 0; i < buflen; i++) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        tmp[i] = (U8)(seed >> 33);
    }
    cpu->memory->memcpyToGuest(bufAddr, tmp, buflen);
    return buflen;
}

// prlimit64(pid, resource, new_limit*, old_limit*) — for v1 we always pretend
// "no limit" by writing RLIM64_INFINITY to old_limit if requested.
static U64 sys_prlimit64_64(CPU64* cpu, U64 /*pid*/, U64 /*res*/, U64 newLim, U64 oldLim) {
    if (oldLim) {
        // struct rlimit64 { __u64 rlim_cur; __u64 rlim_max; }
        cpu->memory->writeq(oldLim, ~0ULL);
        cpu->memory->writeq(oldLim + 8, ~0ULL);
    }
    (void)newLim;
    return 0;
}

// nanosleep(req) and clock_nanosleep(clock, flags, req, rem) share the x86-64
// timespec layout. Route real relative waits through KThread so long sleeps
// yield to other guest threads; absolute waits are converted using the selected
// clock before entering the same path.
static U64 sys_nanosleep64(CPU64* cpu, U64 requestAddr, bool clockSleep,
                           U64 clockId, U64 flags) {
    if (!cpu || !cpu->memory || !cpu->thread || !requestAddr)
        return (U64)(S64)-K_EFAULT;
    if ((!clockSleep && (clockId || flags)) ||
        (clockSleep && ((flags & ~1ULL) ||
         (clockId != 0 && clockId != 1 && clockId != 7 && clockId != 11))))
        return (U64)(S64)-K_EINVAL;

    const U64 seconds = cpu->memory->readq(requestAddr);
    const U64 nanoseconds = cpu->memory->readq(requestAddr + 8);
    if (nanoseconds >= 1000000000ULL || seconds > (~0ULL - nanoseconds) / 1000000000ULL)
        return (U64)(S64)-K_EINVAL;
    U64 duration = seconds * 1000000000ULL + nanoseconds;
    if (clockSleep && (flags & 1)) { // TIMER_ABSTIME
        U64 now = 0;
        if (clockId == 0 || clockId == 11) {
            now = KSystem::getSystemTimeAsMicroSeconds() * 1000ULL;
        } else {
            now = (U64)KSystem::getMilliesSinceStart() * 1000000ULL;
        }
        duration = duration > now ? duration - now : 0;
    }
    const U32 result = cpu->thread->nanoSleep(duration);
    return (U64)(S64)(S32)result;
}

// The emulator exposes one virtual processor and one NUMA node.
static U64 sys_getcpu64(CPU64* cpu, U64 cpuPtr, U64 nodePtr, U64 /*cachePtr*/) {
    if (!cpu || !cpu->memory) return (U64)(S64)-K_EFAULT;
    if (cpuPtr) cpu->memory->writed(cpuPtr, 0);
    if (nodePtr) cpu->memory->writed(nodePtr, 0);
    return 0;
}

// clock_gettime(clk, struct timespec*). Returns wall-clock from the host so
// glibc gets monotonically advancing values.
static U64 sys_clock_gettime64(CPU64* cpu, U64 /*clk*/, U64 tsAddr) {
    if (!tsAddr) return (U64)-K_EFAULT;
    U64 us = KSystem::getSystemTimeAsMicroSeconds();
    U64 sec = us / 1000000ULL;
    U64 nsec = (us % 1000000ULL) * 1000ULL;
    cpu->memory->writeq(tsAddr, sec);
    cpu->memory->writeq(tsAddr + 8, nsec);
    return 0;
}

// read/write/open/close — wired to the existing 32-bit KProcess FD table via
// a bounce buffer. The kobject->readNative path is host-pointer, so the
// 64-bit guest address never has to flow through the 32-bit memory layer.
// Reads from fd 0 still return 0 (EOF) so apps that probe stdin don't hang.
static U64 sys_read64(CPU64* cpu, U64 fd, U64 buf, U64 count) {
    if (fd == 0) return 0;
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)fd);
    if (!fdesc) return (U64)-9; // -EBADF
    if (!fdesc->canRead()) return (U64)-K_EINVAL;
    if (count == 0) return 0;
    // Clamp only to a sane ceiling (was a 1MB cap — too small: wine's PE loader
    // reads whole multi-MB sections in one read() and doesn't loop on a short
    // return, so capping left section tails zero-filled -> the HxD 0x937b30 crash).
    if (count > (256ULL << 20)) count = 256ULL << 20;
    std::vector<U8> tmp((size_t)count);
    // BW64_WSREAD witness: for a wineserver (record-oriented, non-XWire) unix
    // socket, capture the recv-buffer fill before/after the read so the dump can
    // reveal a read that returns N>0 without the cursor advancing by N (a
    // re-read of the same request bytes — the suspected double-free driver).
    KUnixSocketObject* wsSock = nullptr;
    size_t recvBefore = 0;
    if (wsReadEnabled() && cpu->thread && cpu->thread->process) {
        KUnixSocketObject* us = dynamic_cast<KUnixSocketObject*>(fdesc->kobject.get());
        if (us && !us->isXWire()) { wsSock = us; recvBefore = us->debugRecvUsed(); }
    }
    // BW64_WSCONC concurrency witness: for a wineserver process, log this read
    // with the guest thread id. If two different thread ids show overlapping
    // reads of the SAME socket, wineserver is processing requests on >1 thread —
    // the teardown double-release race. We log the live thread count for pid so
    // ">1 thread for wineserver" is visible directly.
    bool wsConcThis = false;
    if (wsConcEnabled() && cpu->thread && cpu->thread->process &&
        cpu->thread->process->exe.contains("wineserver")) {
        KUnixSocketObject* us = dynamic_cast<KUnixSocketObject*>(fdesc->kobject.get());
        if (us && !us->isXWire()) {
            wsConcThis = true;
            int now = ++g_wsConcInflight;
            int prevMax = g_wsConcMax.load();
            while (now > prevMax && !g_wsConcMax.compare_exchange_weak(prevMax, now)) {}
            if (now > 1) {
                klog_fmt("WSCONC: %d host threads concurrently in wineserver socket read! pid=%d tid=%d fd=%d",
                         now, (int)cpu->thread->process->id, (int)cpu->thread->id, (int)fd);
            }
        }
    }
    // Serialize against sys_mmap64_file (and other reads) on the shared per-zip
    // stream — see g_fileReadMutex. A KFile read that repositions a zip's shared
    // unzFile must not interleave with another thread's read/mmap of the same zip.
    // ONLY lock for real files (KFile): a blocking socket/pipe read must NOT hold
    // this global lock or it stalls every other process's file reads (deadlock).
    U32 got;
    {
        std::shared_ptr<KFile> rkfile = std::dynamic_pointer_cast<KFile>(fdesc->kobject);
        if (rkfile) {
            std::lock_guard<std::mutex> lk(g_fileReadMutex);
            // Loop to satisfy the full request: a single zip-backed readNative
            // (unzReadCurrentFile) may return fewer bytes than asked, and the PE
            // loader that issues a whole-section read() does not loop itself.
            // KFiles are seekable regular files, so looping until EOF is correct.
            U64 total = 0;
            got = 0;
            while (total < count) {
                U32 n = fdesc->kobject->readNative(tmp.data() + total, (U32)(count - total));
                if ((S32)n < 0) { got = n; break; }       // propagate error
                if (n == 0) break;                          // EOF
                total += n;
            }
            if ((S32)got >= 0) got = (U32)total;
        } else {
            // Socket/pipe: a short read is correct (blocking semantics) — single shot.
            got = fdesc->kobject->readNative(tmp.data(), (U32)count);
        }
    }
    if (wsConcThis) { --g_wsConcInflight; }
    if ((S32)got < 0) {
        return (U64)(S64)(S32)got; // sign-extend kernel errno
    }
    if (got > 0) {
        cpu->memory->memcpyToGuest(buf, tmp.data(), got);
        // PE DLL section bodies arrive here (read-into-anonymous, not fd-mmap) —
        // register the range so a sampled RIP resolves to wined3d.dll+off, etc.
        ripSamplerNotePeRead(cpu, fdesc, buf, got);
    }
    if (wsSock) {
        size_t recvAfter = wsSock->debugRecvUsed();
        crashRingRecordRead('R', cpu->thread->process->id, (U32)fd, tmp.data(), got,
                            (U32)recvBefore, (U32)recvAfter);
    }
    if (getenv("BW64_IPCDUMP")) {
        // First bytes help identify wineserver reply headers vs pipe wakeup
        // bytes. BW64_IPCWIDE dumps up to 48 bytes so a select_reply's apc_call
        // body (past the 8-byte reply_header) is visible, not just the header.
        int cap = getenv("BW64_IPCWIDE") ? 48 : 16;
        char hex[160] = {0}; int n = (int)((got < (U32)cap) ? got : (U32)cap);
        for (int i = 0; i < n; i++) snprintf(hex + i*3, 4, "%02x ", tmp[i]);
        klog_fmt("IPC [pid=%d] read(fd=%d,count=%llu) -> %d  [%s]",
                 (int)cpu->thread->process->id, (int)fd,
                 (unsigned long long)count, (int)got, hex);
    }
    return (U64)got;
}

// openat(dirfd, path, flags, mode). For dirfd we honour AT_FDCWD (-100) and
// any "absolute" path. Relative paths against a real dirfd aren't supported
// yet — ld-linux always passes AT_FDCWD or absolute, so this covers the
// startup path.
#ifndef K_AT_FDCWD
#define K_AT_FDCWD (-100)
#endif
static U64 sys_openat64(CPU64* cpu, U64 dirfd, U64 pathAddr, U64 flags, U64 /*mode*/) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    if (!pathAddr) return (U64)-K_EFAULT;
    char path[1024] = {0};
    cpu->memory->memcpyFromGuest(path, pathAddr, sizeof(path) - 1);
    KProcess* process = cpu->thread->process.get();
    // /proc/self/exe (and /proc/<pid>/exe): the kernel exposes the executable as
    // a magic symlink. wine's loader open()s/realpath()s it to find its install
    // dir; redirect to the real backing file so it resolves to the loader path
    // instead of a bogus "/proc"-derived prefix. (readlink/readlinkat handle the
    // symlink-read form separately.)
    BString self = B("/proc/self/exe");
    BString selfPid = B("/proc/") + BString::valueOf(process->id) + B("/exe");
    if (self == path || selfPid == path) {
        std::memset(path, 0, sizeof(path));
        std::strncpy(path, process->exe.c_str(), sizeof(path) - 1);
    }
    // BW64_DLLPATH: trace the loader's DLL/dir search for the failing first
    // process — logs every path that mentions the wine install dir, system32,
    // or a builtin DLL, so we can see WHICH search dirs ntdll constructs (the
    // bootstrap process never probes x86_64-windows -> "could not load kernel32").
    if (getenv("BW64_DLLPATH") && (strstr(path, "wine") || strstr(path, "system32") ||
                                   strstr(path, "x86_64-windows"))) {
        klog_fmt("DLLPATH pid=%d openat('%s') flags=0x%llx", (int)process->id, path,
                 (unsigned long long)flags);
    }
    bool isAbs = (path[0] == '/');
    if (!isAbs && (S32)dirfd != K_AT_FDCWD) {
        klog_fmt("sys_openat64: relative path '%s' with dirfd=%d not yet supported",
                 path, (int)(S32)dirfd);
        return (U64)-2;
    }
    KFileDescriptorPtr result;
    U32 rc = process->openFile(process->currentDirectory, BString::copy(path),
                               (U32)flags, result);
    if ((S32)rc < 0) {
        if (getenv("BW64_DLLTRACE") && (strstr(path, ".dll") || strstr(path, ".exe") ||
                                        strstr(path, "x86_64-windows") || strstr(path, "kernel32"))) {
            klog_fmt("DLLTRACE pid=%d FAIL open('%s') rc=%d cwd='%s'",
                     (int)process->id, path, (int)(S32)rc, process->currentDirectory.c_str());
        } else if (getenv("BW64_SCDUMP")) {
            BString full = Fs::getFullPath(process->currentDirectory, BString::copy(path));
            klog_fmt("sys_openat64: open('%s') -> %d  [cwd='%s' full='%s' flags=0x%llx]",
                     path, (int)(S32)rc, process->currentDirectory.c_str(), full.c_str(),
                     (unsigned long long)flags);
        } else if (strstr(path, ".exe")) {
            // Program-spawn failures stay visible by default (rare, and probes
            // + users rely on the exe name appearing in the log). The DLL
            // search-probe spam (dozens of failed opens per library) is
            // SCDUMP-gated: it cost ~20% of main-thread CPU in profiles via
            // print + postMessage + textarea mirror per line.
            klog_fmt("sys_openat64: open('%s') -> %d", path, (int)(S32)rc);
        }
        return (U64)(S64)(S32)rc;
    }
    if (getenv("BW64_DLLTRACE") && (strstr(path, ".dll") || strstr(path, ".exe") ||
                                    strstr(path, "x86_64-windows") || strstr(path, "x86_64-unix"))) {
        klog_fmt("DLLTRACE pid=%d OPEN  '%s' -> fd %d", (int)process->id, path, (int)result->handle);
    }
    // BW64_DIRTRACE: log every O_DIRECTORY open (flags&0x10000) so we can see
    // exactly which directories the failing loader enumerates while hunting for
    // kernel32.dll, and whether the open itself succeeds.
    if (getenv("BW64_DIRTRACE") && ((flags & 0x10000) != 0)) {
        klog_fmt("DIRTRACE pid=%d OPENDIR '%s' -> fd %d (flags=0x%llx)",
                 (int)process->id, path, (int)result->handle, (unsigned long long)flags);
    }
    // Feed the GUI loading screen's activity log: surface the meaningful things
    // wine loads during the boot storm (DLLs/EXEs, the graphics/font stack) so
    // the user can SEE what's loading instead of a stuck bar. Cheap string
    // checks; only fires while the loading screen is active (percent >= 0).
    if (KSystem::bootProgressPercent >= 0) {
        const char* base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (strstr(path, ".dll") || strstr(path, ".exe") ||
            strstr(path, "winex11") || strstr(path, "freetype") ||
            strstr(path, "fontconfig") || strstr(path, ".so")) {
            KSystem::noteBootLog(B("Loading ") + BString::copy(base));
        }
    }
    if (getenv("BW64_SCDUMP")) {
        // Log successful opens of the GUI driver modules so we can see whether
        // wine actually loads its x11 backend (winex11.so/.drv, win32u, the
        // display drivers) vs falling back to the null driver.
        if (strstr(path, "winex11") || strstr(path, "win32u") ||
            strstr(path, ".drv")    || strstr(path, "x11")) {
            klog_fmt("sys_openat64: OPENED '%s' -> fd %d", path, (int)result->handle);
        }
    }
    // Process-spawn signal: successful .exe opens are rare (one per process)
    // and worth one line by default (probes detect EXE SPAWN from this even
    // when the open succeeds, e.g. the program exists in the game zip).
    if (!getenv("BW64_DLLTRACE") && strstr(path, ".exe")) {
        klog_fmt("sys_openat64: OPENED '%s' -> fd %d", path, (int)result->handle);
    }
    return (U64)result->handle;
}

static U64 sys_close64(CPU64* cpu, U64 fd) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    // BW64_SOCKDTOR: witness guest close() of a connected stream socket so we can
    // tell a LEGITIMATE close(2) (wine really closed the fd) from a SPURIOUS GC
    // (the object destructs while the fd is conceptually still open). Pairs with
    // the SOCKDTOR dtor witness — match the kobject pointer across both.
    if (std::getenv("BW64_SOCKDTOR")) {
        KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)fd);
        if (fdesc && fdesc->kobject && fdesc->kobject->type == KTYPE_UNIX_SOCKET) {
            // use_count BEFORE the close is the decisive A-vs-B signal:
            //   ==2 (this fdesc->kobject + the local fdesc copy held here) => this
            //        close drops the LAST process ref -> the object destructs now
            //        -> no other process's fd holds it => MODEL A (shared object).
            //   > 2 => another fd table still references the SAME object, so this
            //        close should NOT destruct it; the kill must be the peer-EOF
            //        cascade from the OTHER end's dtor => MODEL B (peer cascade).
            // (We hold one extra ref via `fdesc` here, so subtract it mentally:
            //  raw==2 means exactly one real holder remains = the fd being closed.)
            long uc = fdesc->kobject.use_count();
            KUnixSocketObject* us = dynamic_cast<KUnixSocketObject*>(fdesc->kobject.get());
            void* peer = nullptr;
            int peerIn = -1, peerOut = -1, peerConn = -1;
            int myIn = -1, myOut = -1, myConn = -1;
            if (us) {
                myIn = (int)us->inClosed; myOut = (int)us->outClosed; myConn = (int)us->connected;
                std::shared_ptr<KUnixSocketObject> con = us->connection.lock();
                if (con) {
                    peer = (void*)con.get();
                    peerIn = (int)con->inClosed; peerOut = (int)con->outClosed; peerConn = (int)con->connected;
                }
            }
            klog_fmt("SOCKCLOSE: pid=%d tid=%d close(fd=%llu) kobj=%p use_count=%ld(raw,-1=fdesc) "
                     "self{in=%d out=%d conn=%d} peer=%p peer{in=%d out=%d conn=%d}",
                     (int)cpu->thread->process->id, (int)cpu->thread->id,
                     (unsigned long long)fd, (void*)fdesc->kobject.get(),
                     uc, myIn, myOut, myConn, peer, peerIn, peerOut, peerConn);
        }
    }
    return (U64)(S64)(S32)cpu->thread->process->close((FD)fd);
}

// getdents64(fd, dirp, count) — read directory entries into the guest buffer
// in Linux struct linux_dirent64 layout:
//   u64 d_ino; s64 d_off; u16 d_reclen; u8 d_type; char d_name[]; (NUL-term)
// each record padded to 8 bytes. Mirrors KProcess::getdents' is64 path but
// writes through KMemory64 (the 32-bit version's dirp is a U32 address). The
// FsOpenNode file pointer tracks how far we've iterated, so successive calls
// page through the directory and a final call returns 0 (end).
static U64 sys_getdents64_real(CPU64* cpu, U64 fd, U64 dirp, U64 count) {
    // NULL buffer is EFAULT regardless of process state (matches the kernel
    // contract and the existing self-test, which runs with no real process).
    if (!dirp) return (U64)-K_EFAULT;
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    bool dt = getenv("BW64_DIRTRACE") != nullptr;
    int dtpid = (int)cpu->thread->process->id;
    KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)fd);
    if (!fdesc) { if (dt) klog_fmt("DIRTRACE pid=%d getdents64 fd=%llu -> EBADF (no fdesc)", dtpid, (unsigned long long)fd); return (U64)-9; } // -EBADF
    std::shared_ptr<KFile> kfile = std::dynamic_pointer_cast<KFile>(fdesc->kobject);
    if (!kfile || !kfile->openFile || !kfile->openFile->node) { if (dt) klog_fmt("DIRTRACE pid=%d getdents64 fd=%llu -> ENOTDIR (no kfile/openFile/node)", dtpid, (unsigned long long)fd); return (U64)-K_ENOTDIR; }
    FsOpenNode* openNode = kfile->openFile;
    if (!openNode->node->isDirectory()) { if (dt) klog_fmt("DIRTRACE pid=%d getdents64 path='%s' -> ENOTDIR (not a directory)", dtpid, openNode->node->path.c_str()); return (U64)-K_ENOTDIR; }

    U32 entries = openNode->getDirectoryEntryCount();
    U64 len = 0;
    U64 pos = dirp;
    // BW64_DIRTRACE: log each getdents64 directory's path, entry count, file
    // pointer (resume index), and whether kernel32.dll is among the names. The
    // residual "could not load kernel32.dll" failure loops openat(O_DIRECTORY)+
    // getdents64 over the wine builtin DLL dir — this proves whether the listing
    // actually surfaces kernel32.dll for the failing process.
    if (getenv("BW64_DIRTRACE")) {
        bool hasK32 = false; int found = 0;
        for (U32 j = 0; j < entries; j++) {
            BString nm; std::shared_ptr<FsNode> e = openNode->getDirectoryEntry(j, nm);
            if (!e) continue;
            found++;
            if (strcasecmp(nm.c_str(), "kernel32.dll") == 0) hasK32 = true;
        }
        klog_fmt("DIRTRACE pid=%d path='%s' entries=%u realFound=%d fp=%u kernel32=%d",
                 (int)cpu->thread->process->id,
                 openNode->node->path.c_str(), entries, found,
                 (U32)openNode->getFilePointer(), hasK32 ? 1 : 0);
    }
    for (U32 i = (U32)openNode->getFilePointer(); i < entries; i++) {
        BString name;
        std::shared_ptr<FsNode> entry = openNode->getDirectoryEntry(i, name);
        if (!entry) continue;
        U32 nameLen = (U32)strlen(name.c_str());
        // d_ino(8) d_off(8) d_reclen(2) d_type(1) name(nameLen) NUL(1)
        U32 recordLen = 20 + nameLen;        // 19 header bytes + name + NUL, but
        recordLen = (recordLen + 7) / 8 * 8; // 8-byte aligned (d_off must align)
        if (len + recordLen > count) {
            if (len == 0) return (U64)-K_EINVAL; // buffer too small for one entry
            break;                                // resume here next call
        }
        cpu->memory->writeq(pos,      (U64)entry->id);       // d_ino
        cpu->memory->writeq(pos + 8,  (U64)(i + 1));         // d_off (next pos)
        cpu->memory->writew(pos + 16, (U16)recordLen);       // d_reclen
        cpu->memory->writeb(pos + 18, (U8)entry->getType(true)); // d_type
        cpu->memory->memcpyToGuest(pos + 19, name.c_str(), nameLen + 1); // name+NUL
        pos += recordLen;
        len += recordLen;
        openNode->seek(i + 1);
    }
    return len;
}

// lseek — wired straight to KObject::seek.
static U64 sys_lseek64(CPU64* cpu, U64 fd, U64 offset, U64 whence) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)fd);
    if (!fdesc) return (U64)-9;
    S64 off = (S64)offset;
    S64 pos;
    if (whence == 0) { // SEEK_SET
        pos = fdesc->kobject->seek(off);
    } else if (whence == 1) { // SEEK_CUR
        pos = fdesc->kobject->getPos() + off;
        pos = fdesc->kobject->seek(pos);
    } else if (whence == 2) { // SEEK_END
        pos = fdesc->kobject->length() + off;
        pos = fdesc->kobject->seek(pos);
    } else {
        return (U64)-K_EINVAL;
    }
    return (U64)pos;
}

// pread64(fd, buf, count, offset) — read at an absolute offset without moving
// the file position. glibc's ld.so uses it to read a DSO's program headers
// before mmap. Emulated as save-pos/seek/read/restore over the KObject layer.
static U64 sys_pread64(CPU64* cpu, U64 fd, U64 buf, U64 count, U64 offset) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)fd);
    if (!fdesc) return (U64)-9; // -EBADF
    if (!fdesc->canRead()) return (U64)-K_EINVAL;
    if (count == 0) return 0;
    // Do NOT cap `count`: wine's PE loader preads an ENTIRE section in one call
    // (HxD's .text is ~5.3MB) and does NOT loop on a short return, so a small cap
    // left the section tail zero-filled — a function/thread-proc whose code lives
    // past the cap then executed zeros and crashed (the HxD "jump into 0x937b30"
    // bug: the proc was 4.3MB into a .text formerly capped at 1MB). Clamp only to
    // a sane ceiling to avoid a pathological allocation from a bogus count.
    if (count > (256ULL << 20)) count = 256ULL << 20;
    std::vector<U8> tmp((size_t)count);
    // Loop readNative until we've satisfied `count` or hit EOF: a single zip-backed
    // readNative (unzReadCurrentFile) can legally return FEWER bytes than asked,
    // and our single-shot callers (the PE loader) don't loop themselves.
    auto readFully = [&](void) -> S64 {
        S64 savedPos = fdesc->kobject->getPos();
        fdesc->kobject->seek((S64)offset);
        U64 total = 0;
        while (total < count) {
            U32 got = fdesc->kobject->readNative(tmp.data() + total, (U32)(count - total));
            if ((S32)got < 0) { fdesc->kobject->seek(savedPos); return (S64)(S32)got; }
            if (got == 0) break; // EOF
            total += got;
        }
        fdesc->kobject->seek(savedPos);
        return (S64)total;
    };
    // Use KFile's positioned operation so concurrent lseek and ordinary reads
    // share its filePosMutex. A socket must never hold the global file lock.
    std::shared_ptr<KFile> pkfile = std::dynamic_pointer_cast<KFile>(fdesc->kobject);
    S64 got;
    if (pkfile) {
        std::lock_guard<std::mutex> lk(g_fileReadMutex);
        U64 total = 0;
        got = 0;
        while (total < count) {
            U32 n = pkfile->preadNative(tmp.data() + total, (S64)(offset + total), (U32)(count - total));
            if ((S32)n < 0) { got = (S64)(S32)n; break; }
            if (n == 0) break;
            total += n;
        }
        if (got >= 0) got = (S64)total;
    } else {
        got = readFully();
    }
    if (got < 0) return (U64)got;
    if (got > 0) {
        cpu->memory->memcpyToGuest(buf, tmp.data(), (U64)got);
        // PE DLL section bodies arrive here too (the loader preads whole sections
        // into anonymous guest memory) — register for RIP resolution.
        ripSamplerNotePeRead(cpu, fdesc, buf, (U64)got);
    }
    return (U64)got;
}

// pwrite64(fd, buf, count, offset) — write at an absolute offset without moving
// the file position. wineserver uses it to populate registry/config files in
// the prefix (it pwrite's records into the .reg/server files). Emulated as
// save-pos/seek/write/restore over the KObject layer, mirroring sys_pread64.
static U64 sys_pwrite64(CPU64* cpu, U64 fd, U64 buf, U64 count, U64 offset) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)fd);
    if (!fdesc) return (U64)-9; // -EBADF
    if (!fdesc->canWrite()) return (U64)-K_EINVAL;
    if (count == 0) return 0;
    // Ceiling only (was a 1MB cap that silently truncated large pwrites).
    if (count > (256ULL << 20)) count = 256ULL << 20;
    std::vector<U8> tmp((size_t)count);
    cpu->memory->memcpyFromGuest(tmp.data(), buf, (U32)count);
    S64 savedPos = fdesc->kobject->getPos();
    fdesc->kobject->seek((S64)offset);
    U32 wrote = fdesc->kobject->writeNative(tmp.data(), (U32)count);
    fdesc->kobject->seek(savedPos);
    return (U64)(S64)(S32)wrote;
}

// writeStatBuf64 — write the x86-64 Linux struct stat (144 bytes) into
// guest memory. Field offsets match the canonical glibc/kernel layout for
// __NR_fstat / __NR_newfstatat result buffers on x86-64.
static void writeStatBuf64(KMemory64* mem, U64 addr, U64 size, U32 mode,
                            U64 ino, U32 uid, U32 gid, U64 mtime, U64 rdev) {
    U8 buf[144];
    std::memset(buf, 0, sizeof(buf));
    auto put64 = [&](U32 off, U64 v) { std::memcpy(buf + off, &v, 8); };
    auto put32 = [&](U32 off, U32 v) { std::memcpy(buf + off, &v, 4); };
    put64(0,  1);             // st_dev (fake)
    put64(8,  ino);           // st_ino
    put64(16, 1);             // st_nlink
    put32(24, mode);          // st_mode
    put32(28, uid);           // st_uid
    put32(32, gid);           // st_gid
    put32(36, 0);             // __pad0
    put64(40, rdev);          // st_rdev — REAL device id. Wine's ntdll debug init
                              // (dlls/ntdll/unix/debug.c init_options) disables ALL
                              // debug output when fstat(2) is a char device whose
                              // st_rdev equals /dev/null's — with the old hardcoded 0
                              // that guard tripped for /dev/tty0 and WINEDEBUG was
                              // silently ignored in every guest process (O2).
    put64(48, size);          // st_size
    put64(56, 4096);          // st_blksize
    put64(64, (size + 511) / 512); // st_blocks (512-byte units)
    put64(72, mtime);         // st_atime
    put64(80, 0);
    put64(88, mtime);         // st_mtime
    put64(96, 0);
    put64(104, mtime);        // st_ctime
    put64(112, 0);
    mem->memcpyToGuest(addr, buf, sizeof(buf));
}

// Forward decl — sys_newfstatat64 may delegate to sys_fstat64 for AT_EMPTY_PATH.
static U64 sys_fstat64(CPU64* cpu, U64 fd, U64 statbuf);

// Path-based stat shared by stat/lstat/newfstatat. followSymlink controls
// the lstat vs stat distinction (Fs::getNodeFromLocalPath's third arg).
static U64 sys_stat_path64(CPU64* cpu, U64 pathAddr, U64 statbuf, bool followSymlink) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    if (!pathAddr || !statbuf) return (U64)-K_EFAULT;
    char path[1024] = {0};
    cpu->memory->memcpyFromGuest(path, pathAddr, sizeof(path) - 1);
    BString bpath = BString::copy(path);
    std::shared_ptr<FsNode> node = Fs::getNodeFromLocalPath(
        cpu->thread->process->currentDirectory, bpath, followSymlink);
    if (getenv("BW64_SYSTRACE")) {
        klog_fmt("sys_stat_path64: '%s' (cwd='%s') -> %s", path,
                 cpu->thread->process->currentDirectory.c_str(),
                 node ? "OK" : "ENOENT");
    }
    if (!node) return (U64)-2; // -ENOENT
    U64 size  = node->length();
    U32 mode  = node->getMode();
    U64 ino   = node->id;
    U64 mtime = node->lastModified() / 1000; // ms → seconds
    writeStatBuf64(cpu->memory, statbuf, size, mode, ino, 1000, 1000, mtime, node->rdev);
    return 0;
}

// newfstatat(dirfd, path, statbuf, flags). AT_EMPTY_PATH (0x1000) means
// "stat the dirfd itself"; AT_SYMLINK_NOFOLLOW (0x100) makes it lstat.
#ifndef K_AT_SYMLINK_NOFOLLOW
#define K_AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef K_AT_EMPTY_PATH
#define K_AT_EMPTY_PATH 0x1000
#endif
static U64 sys_newfstatat64(CPU64* cpu, U64 dirfd, U64 pathAddr, U64 statbuf, U64 flags) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    if (!statbuf) return (U64)-K_EFAULT;
    // AT_EMPTY_PATH with NULL/"" path means stat the fd.
    if ((flags & K_AT_EMPTY_PATH) && (!pathAddr || cpu->memory->readb(pathAddr) == 0)) {
        return sys_fstat64(cpu, dirfd, statbuf);
    }
    // We only honour AT_FDCWD or absolute paths for now (matches openat).
    char path[1024] = {0};
    cpu->memory->memcpyFromGuest(path, pathAddr, sizeof(path) - 1);
    bool isAbs = (path[0] == '/');
    if (getenv("BW64_SYSTRACE")) {
        klog_fmt("sys_newfstatat64: dirfd=%d path='%s' flags=0x%llx",
                 (int)(S32)dirfd, path, (unsigned long long)flags);
    }
    if (!isAbs && (S32)dirfd != K_AT_FDCWD) {
        return (U64)-2;
    }
    return sys_stat_path64(cpu, pathAddr, statbuf, !(flags & K_AT_SYMLINK_NOFOLLOW));
}

static U64 sys_fstat64(CPU64* cpu, U64 fd, U64 statbuf) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    if (!statbuf) return (U64)-K_EFAULT;
    KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)fd);
    if (!fdesc) {
        return (U64)-9; // -EBADF
    }
    // Reach for the underlying KFile to ask the FsNode for metadata.
    std::shared_ptr<KFile> kfile = std::dynamic_pointer_cast<KFile>(fdesc->kobject);
    U64 size = 0;
    U32 mode = 0100644; // S_IFREG | 0644
    U64 ino = 0;
    U64 mtime = 0;
    if (kfile && kfile->openFile) {
        size = (U64)kfile->openFile->length();
        if (kfile->openFile->node) {
            mode  = kfile->openFile->node->getMode();
            ino   = kfile->openFile->node->id;
            mtime = (U64)kfile->openFile->node->lastModified() / 1000; // ms -> Linux seconds
        }
    } else {
        // Non-file kobject (socket/pipe): claim S_IFCHR so callers don't
        // assume seekable.
        mode = 0020666;
    }
    // Real st_rdev for device nodes (tty0 etc.); 0 for regular files/pipes —
    // identical to the old behavior except for char/block devices. Without the
    // node's rdev, wine's debug init mistakes tty fds for /dev/null and drops
    // every WINEDEBUG channel (see writeStatBuf64's st_rdev note).
    U64 rdev = (kfile && kfile->openFile && kfile->openFile->node) ? kfile->openFile->node->rdev : 0;
    writeStatBuf64(cpu->memory, statbuf, size, mode, ino, 1000, 1000, mtime, rdev);
    return 0;
}

// File-backed mmap. Reads the requested file region into freshly mmap'd
// pages. Not lazy/COW — eager copy is simple and bounded for ld-linux's
// typical 200 KiB lib mapping.
static U64 sys_mmap64_file(CPU64* cpu, U64 addr, U64 length, U64 prot,
                           U64 flags, U64 fd, U64 offset) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)fd);
    if (!fdesc) return (U64)-9; // -EBADF
    std::shared_ptr<KFile> kfile = std::dynamic_pointer_cast<KFile>(fdesc->kobject);
    if (!kfile || !kfile->openFile) {
        return (U64)-K_ENOSYS;
    }
    bool reserved = false;
    if (addr == 0) {
        // ld.so's DSO-span reservation (mmap(NULL, span, PROT_NONE, fd)) comes
        // through here; it MUST draw from the same process-wide cursor as
        // anonymous maps so the FIXED sub-segment maps that follow land on
        // disjoint, uncorrupted pages. Atomically reserve+map (closes the
        // MT TOCTOU race two sibling threads hit via the old scan-then-map).
        addr = cpu->memory->mmapReserveAndMap(length, (U32)prot);
        reserved = true; // pages already mapped under the lock; don't re-map/zero
    }
    U64 aligned = addr & ~0xFFFULL;
    U64 mapLen = (length + (addr - aligned) + 0xFFF) & ~0xFFFULL;
    bool dump = getenv("BW64_FMMAP") != nullptr;
    if (dump) {
        klog_fmt("FMMAP [pid=%d] addr=0x%llx aligned=0x%llx len=0x%llx mapLen=0x%llx "
                 "prot=0x%x flags=0x%x off=0x%llx fixed=%d inode=%llu file='%s'",
                 (int)(cpu->thread ? cpu->thread->process->id : -1),
                 (unsigned long long)addr, (unsigned long long)aligned,
                 (unsigned long long)length, (unsigned long long)mapLen,
                 (unsigned)prot, (unsigned)flags, (unsigned long long)offset,
                 (int)((flags & K_MAP_FIXED) != 0),
                 (unsigned long long)(kfile->openFile->node ? kfile->openFile->node->id : 0),
                 kfile->openFile->node ? kfile->openFile->node->path.c_str() : "?");
    }
    // Honor the requested mapping mode for every file, independent of its
    // pathname. Wine 8's map_image_into_view uses VPROT_WRITECOPY/MAP_PRIVATE
    // for ordinary PE image sections; shared sections must remain shared.
    // Reference: wine-8.0/dlls/ntdll/unix/virtual.c, map_file_into_view.
    if ((flags & K_MAP_SHARED) && kfile->openFile->node) {
        // mmap(NULL, MAP_SHARED) has already reserved a fresh address range.
        // Replace that range's pages with shared backing as well: Wine's server
        // uses NULL for its writable clock page, while clients use fixed maps.
        // Seed bytes for any page of this file not yet in the registry.
        std::vector<U8> seedBuf((size_t)mapLen, 0);
        U64 fileBase = offset & ~0xFFFULL;
        {
            std::lock_guard<std::mutex> lk(g_fileReadMutex);
            U64 total = 0;
            while (total < mapLen) {
                U32 n = kfile->preadNative(seedBuf.data() + total, (S64)(fileBase + total), (U32)(mapLen - total));
                if ((S32)n < 0) return (U64)(S64)(S32)n;
                if (n == 0) break;
                total += n;
            }
        }
        U64 r = cpu->memory->mmapSharedFile(aligned, mapLen, (U32)prot,
                                            kfile->openFile->node->id,
                                            kfile->openFile->node->path.c_str(),
                                            fileBase, seedBuf.data(), (U64)seedBuf.size());
        if ((S64)r < 0) return r;
        if (kfile->openFile->node) {
            if (ripSamplerModuleTrackingEnabled()) {
                ripSamplerNoteModule((int)cpu->thread->process->id, aligned, mapLen,
                                     kfile->openFile->node->path.c_str());
            }
            if (bwGuestProfileEnabled()) {
                bwGuestProfileNoteModule((int)cpu->thread->process->id, aligned, mapLen,
                                         kfile->openFile->node->path.c_str());
            }
        }
        return addr;
    }
    // The page that contains `addr` may already hold valid bytes from a PRIOR
    // overlapping mapping of the SAME DSO (glibc maps the first PT_LOAD over the
    // whole span, then MAP_FIXED-remaps later segments whose file offset is not
    // page-aligned — so [aligned, addr) is the tail of the previous segment).
    // mmapAnonymousFixed ZEROES whole pages, so a blind zero here wipes that
    // tail and never refills it (we only read file content into [addr, ...)),
    // corrupting live code/data. Preserve the leading partial-page bytes across
    // the (re)map: snapshot [aligned, addr) before, restore after.
    // For a FIXED map (addr supplied by ld.so) the target pages may already hold
    // valid bytes from a PRIOR overlapping mapping of the SAME DSO (glibc maps
    // the first PT_LOAD over the whole span, then MAP_FIXED-remaps later segments
    // whose file offset is not page-aligned — so [aligned, addr) is the tail of
    // the previous segment). mmapAnonymousFixed ZEROES whole pages, so a blind
    // zero wipes that tail and never refills it (we only read file content into
    // [addr, ...)), corrupting live code/data. Preserve the partial-page head &
    // tail across the (re)map. SKIPPED when `reserved`: mmapReserveAndMap already
    // mapped a fresh, disjoint range under the lock — nothing to preserve.
    if (!reserved) {
        U64 head = addr - aligned;
        std::vector<U8> headSave;
        if (head > 0 && cpu->memory->isPageMapped(aligned >> 12)) {
            headSave.resize((size_t)head);
            cpu->memory->memcpyFromGuest(headSave.data(), aligned, head);
            if (dump) klog_fmt("FMMAP   preserving 0x%llx head bytes at 0x%llx",
                               (unsigned long long)head, (unsigned long long)aligned);
        }
        U64 fileEnd = addr + length;
        U64 mapEnd  = aligned + mapLen;
        U64 tail = (mapEnd > fileEnd) ? (mapEnd - fileEnd) : 0;
        std::vector<U8> tailSave;
        if (tail > 0 && cpu->memory->isPageMapped((fileEnd) >> 12)) {
            tailSave.resize((size_t)tail);
            cpu->memory->memcpyFromGuest(tailSave.data(), fileEnd, tail);
        }
        U64 mapped = cpu->memory->mmapAnonymousFixed(aligned, mapLen, (U32)prot);
        if ((S64)mapped < 0) {
            return mapped;
        }
        if (!headSave.empty()) cpu->memory->memcpyToGuest(aligned, headSave.data(), head);
        if (!tailSave.empty()) cpu->memory->memcpyToGuest(fileEnd, tailSave.data(), tail);
    }
    // Read [offset, offset+length) and copy into [addr, addr+length).
    // KFile's positioned read holds the same filePosMutex as ordinary reads
    // and lseek. The global file-read lock alone cannot exclude lseek, and
    // direct openFile calls bypass KFile's position ownership entirely.
    std::vector<U8> buf((size_t)length);
    U32 got;
    {
        std::lock_guard<std::mutex> lk(g_fileReadMutex);
        // Loop: a single zip-backed readNative (unzReadCurrentFile) can return
        // fewer bytes than `length`. A single-shot read here would leave the tail
        // of a large section (e.g. a multi-MB .text) zero-filled — the same class
        // of bug as the capped pread64 that produced the HxD 0x937b30 crash.
        U64 total = 0;
        while (total < length) {
            U32 n = kfile->preadNative(buf.data() + total, (S64)(offset + total), (U32)(length - total));
            if ((S32)n < 0) return (U64)(S64)(S32)n;
            if (n == 0) break; // EOF / no progress
            total += n;
        }
        got = (U32)total;
    }
    // BW64_MAPWATCH=0xADDR: log every file-mmap that lands in [ADDR, ADDR+0x20000)
    // with its file offset — to catch a segment being (re)mapped from the WRONG
    // file offset (e.g. offset 0 = the MZ header splattered over .text/.data/IAT).
    {
        static const char* mw = std::getenv("BW64_MAPWATCH");
        if (mw) {
            U64 wa = std::strtoull(mw, nullptr, 0);
            if (addr < wa + 0x20000 && addr + length > wa) {
                U64 f8 = 0; if (got >= 8) std::memcpy(&f8, buf.data(), 8);
                klog_fmt("MAPWATCH pid=%d addr=0x%llx len=0x%llx offset=0x%llx prot=0x%x flags=0x%x got=%u first8=0x%llx file='%s'",
                         (int)cpu->thread->process->id, (unsigned long long)addr,
                         (unsigned long long)length, (unsigned long long)offset, (unsigned)prot,
                         (unsigned)flags, got, (unsigned long long)f8,
                         kfile->openFile->node ? kfile->openFile->node->path.c_str() : "?");
            }
        }
    }
    if (got > 0) {
        cpu->memory->memcpyToGuest(addr, buf.data(), got);
    }
    (void)flags;
    // BW64_RIPSAMPLE: record this DSO/PE segment so a sampled spinning RIP can be
    // resolved to file+offset. This is where wine's x86_64-unix .so halves (where
    // the C code that busy-loops lives) become known. Use the same aligned/mapLen
    // span the BW64_FMMAP trace prints above.
    if (kfile->openFile->node) {
        if (ripSamplerModuleTrackingEnabled()) {
            ripSamplerNoteModule((int)cpu->thread->process->id, aligned, mapLen,
                                 kfile->openFile->node->path.c_str());
        }
        if (bwGuestProfileEnabled()) {
            bwGuestProfileNoteModule((int)cpu->thread->process->id, aligned, mapLen,
                                     kfile->openFile->node->path.c_str());
        }
    }
    return addr;
}

// writev — iterate the iovec array, calling write per segment. Each iovec
// entry on x86-64 is { u64 base; u64 len }.
static U64 sys_writev64(CPU64* cpu, U64 fd, U64 iov, U64 iovcnt) {
    U64 total = 0;
    for (U64 i = 0; i < iovcnt; i++) {
        U64 base = cpu->memory->readq(iov + i * 16 + 0);
        U64 len  = cpu->memory->readq(iov + i * 16 + 8);
        if (len == 0) continue;
        S64 wrote = (S64)sys_write64(cpu, fd, base, len);
        if (wrote < 0) {
            return total > 0 ? total : (U64)wrote;
        }
        total += (U64)wrote;
        if ((U64)wrote < len) break; // short write
    }
    return total;
}

// readv(fd, iov, iovcnt) — scatter read. Mirror sys_writev64: walk the
// 64-bit iovec array (struct iovec is {void* base; size_t len} = 16 bytes
// on x86-64) and read into each segment via sys_read64. Stop on the first
// short read (fewer bytes than the segment asked for) — that's EOF / no
// more data right now, same as the kernel. Returns total bytes read, or
// the first segment's error if nothing was read. wine/wineserver read
// server replies through readv, so a missing impl silently breaks the IPC
// reply path (was -ENOSYS).
static U64 sys_readv64(CPU64* cpu, U64 fd, U64 iov, U64 iovcnt) {
    U64 total = 0;
    for (U64 i = 0; i < iovcnt; i++) {
        U64 base = cpu->memory->readq(iov + i * 16 + 0);
        U64 len  = cpu->memory->readq(iov + i * 16 + 8);
        if (len == 0) continue;
        S64 got = (S64)sys_read64(cpu, fd, base, len);
        if (got < 0) {
            return total > 0 ? total : (U64)got;
        }
        total += (U64)got;
        if ((U64)got < len) break; // short read: EOF / drained
    }
    return total;
}

// Read a NUL-terminated guest C-string at addr from 64-bit memory. Bounded so a
// missing terminator can't loop forever; argv/env strings and paths are well
// under this. Reads in small chunks to avoid a huge fixed stack buffer.
static BString readGuestString64(CPU64* cpu, U64 addr) {
    if (!addr) return BString();
    std::string s;
    const U64 MAX = 64 * 1024;
    while (s.size() < MAX) {
        char chunk[256];
        cpu->memory->memcpyFromGuest(chunk, addr, sizeof(chunk));
        U32 n = 0;
        for (; n < sizeof(chunk); n++) {
            if (chunk[n] == 0) { s.append(chunk, n); return BString::copy(s.c_str()); }
        }
        s.append(chunk, sizeof(chunk));
        addr += sizeof(chunk);
    }
    return BString::copy(s.c_str());
}

// Walk a NULL-terminated array of 64-bit guest pointers (argv / envp) and read
// each pointed-to C-string. The 64-bit ABI passes 8-byte pointers (unlike the
// 32-bit execve's 4-byte readd walk).
static void readStringArray64(CPU64* cpu, U64 arrayAddr, std::vector<BString>& out) {
    if (!arrayAddr) return;
    const U32 MAX_ENTRIES = 4096;
    for (U32 i = 0; i < MAX_ENTRIES; i++) {
        U64 p = cpu->memory->readq(arrayAddr);
        if (!p) break;
        arrayAddr += 8;
        out.push_back(readGuestString64(cpu, p));
    }
}

// execve(path, argv, envp) for 64-bit guests. wine64 re-execs itself and spawns
// wineserver64 through this. Marshals the 8-byte-pointer argv/envp arrays out of
// memory64, then drives the shared KProcess::execve, which resets memory and
// re-runs the loader (ElfLoader::loadProgram routes ELF64 -> loadProgram64,
// rebuilding memory64/cpu64 fresh — see KProcess::execve's 64-bit reset). On
// success execve never returns to the caller (the image is replaced); it returns
// -K_CONTINUE which the dispatcher must NOT write into RAX.
static U64 sys_execve64(CPU64* cpu, U64 pathAddr, U64 argvAddr, U64 envpAddr) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    if (!pathAddr) return (U64)-K_EFAULT;
    BString path = readGuestString64(cpu, pathAddr);
    // BW64_RIPSAMPLE: a re-exec replaces this pid's address space, so its old
    // module ranges would mis-attribute the next image's RIPs. Drop them; the
    // new image's segment maps re-populate the table.
    ripSamplerClearPid((int)cpu->thread->process->id);
    bwGuestProfileClearPid((int)cpu->thread->process->id);
    std::vector<BString> args;
    std::vector<BString> envs;
    readStringArray64(cpu, argvAddr, args);
    readStringArray64(cpu, envpAddr, envs);
    // Boot reliability (M18): wine64 auto-spawns a BARE wineserver at boot
    // (argc==1) which races the launcher's later `wineserver64 -p` pin for the
    // server lock. When the bare/transient instance wins (a coin flip), the
    // session's wineserver is non-persistent and shuts down in the first
    // zero-client moment of the slow interpreted boot (the wineboot-chain ->
    // app handoff), cleanly killing explorer and the never-mapped app —
    // ~1-in-2 cold boots died this way. Make every bare wineserver spawn
    // persistent, so whichever instance wins the lock keeps the session alive.
    // Flagged spawns (-p pin, -k, -w) are left untouched (argc > 1).
    if (args.size() == 1 &&
        (path.endsWith("/wineserver64") || path.endsWith("/wineserver"))) {
        args.push_back(B("-p"));
        klog_fmt("sys_execve64: bare wineserver spawn made persistent (-p) for boot reliability");
    }
    klog_fmt("sys_execve64: pid=%d path='%s' argv0='%s' argc=%d envc=%d",
             (int)(cpu->thread && cpu->thread->process ? cpu->thread->process->id : -1),
             path.c_str(),
             args.empty() ? "" : args[0].c_str(), (int)args.size(), (int)envs.size());
    // Full argv dump — invaluable for telling which wine subprocess (wineboot/
    // services.exe/rpcss/plugplay/explorer) is being launched in the re-exec
    // chain; the bare argv0 ('/usr/lib/wine/wine64') is identical for all of them.
    if (getenv("BW64_SYSTRACE") || getenv("BW64_EXECDUMP")) {
        for (size_t i = 1; i < args.size(); i++)
            klog_fmt("sys_execve64:   argv[%d]='%s'", (int)i, args[i].c_str());
    }
    for (auto& e : envs) {
        if (strncmp(e.c_str(), "WINELOADER=", 11) == 0)
            klog_fmt("sys_execve64:   env %s", e.c_str());
        if (getenv("BW64_ENVDUMP"))
            klog_fmt("sys_execve64:   ENV %s", e.c_str());
    }
    // Drive the GUI loading-screen progress: the wine boot chain re-execs
    // wine64 with the next PE in argv (e.g. ".../services.exe", "winedevice.exe",
    // the target "notepad.exe"). Feed each .exe basename to the boot-stage
    // tracker so the XWire present tick can render a labeled progress bar until
    // the guest paints its real window.
    for (auto& a : args) {
        if (a.contains(".exe")) { KSystem::noteBootStage(a); break; }
    }
    return (U64)(S64)(S32)cpu->thread->process->execve(cpu->thread, path, args, envs);
}

// readlink(path, buf, bufsize) — resolve a symlink. /proc/self/exe is the
// big-ticket caller for glibc startup; everything else falls back to the
// FsNode link field if present.
static U64 sys_readlink64(CPU64* cpu, U64 pathAddr, U64 buf, U64 sz) {
    if (!cpu->thread || !cpu->thread->process) return (U64)-K_ENOSYS;
    if (!pathAddr || !buf || sz == 0) return (U64)-K_EFAULT;
    char path[1024] = {0};
    cpu->memory->memcpyFromGuest(path, pathAddr, sizeof(path) - 1);
    KProcess* proc = cpu->thread->process.get();
    BString pidExe = B("/proc/") + BString::valueOf(proc->id) + B("/exe");
    BString resolved;
    // The kernel's /proc magic symlinks. glibc/wine's realpath() walks
    // /proc/self/exe component-by-component, so we must resolve BOTH the
    // intermediate /proc/self (-> the numeric pid dir) and the terminal
    // .../exe (-> the executable's real path). Without /proc/self resolving,
    // realpath bails and wine derives a bogus "/proc"-based install dir.
    if (std::strcmp(path, "/proc/self") == 0 || std::strcmp(path, "/proc/thread-self") == 0) {
        resolved = BString::valueOf(proc->id);                 // relative target, like Linux
    } else if (std::strcmp(path, "/proc/self/exe") == 0 ||
               std::strcmp(path, "/proc/thread-self/exe") == 0 ||
               pidExe == path) {
        resolved = proc->exe;
    } else {
        std::shared_ptr<FsNode> n = Fs::getNodeFromLocalPath(
            proc->currentDirectory, BString::copy(path), false);
        if (!n || !n->isLink()) return (U64)-22; // -EINVAL on non-symlink
        resolved = n->link;
    }
    U64 toCopy = (U64)resolved.length();
    if (toCopy > sz) toCopy = sz;
    cpu->memory->memcpyToGuest(buf, resolved.c_str(), toCopy);
    return toCopy;
}

// Futex with real per-address waiter bookkeeping.
//
// We still cannot truly block (no KThread64 yet), so WAIT returns -EAGAIN
// even when the word matches. But we DO count would-block events per
// address in cpu->futexWaiters, and WAKE drains up to `val` of them and
// returns the count actually drained — matching the kernel's "number
// woken" semantics. The difference from the old stub:
//
//   - A WAKE that follows a WAIT-on-match now returns 1, not 0, which
//     lets glibc's condvar fast path see that its broadcast wasn't a
//     no-op (the old behaviour caused some __pthread_cond_signal paths
//     to loop forever calling FUTEX_WAKE expecting >0).
//   - Waiter counts are addressable per `uaddr`, so WAKE on one futex
//     does not spuriously claim to wake waiters on another.
//
// Storage is freed automatically when count drops to 0 (erase from map).
// Map lives on CPU64 so it's process-scoped — exactly what FUTEX_PRIVATE
// expects. When multi-thread support lands, the map moves to KProcess64
// with no shape change.
//
//   WAIT / WAIT_BITSET (+ _PRIVATE): read 32-bit word at uaddr; if
//       != val return -EAGAIN immediately. If == val, bump waiter count
//       and ALSO return -EAGAIN (would-block placeholder). The bumped
//       count is consumed by the next WAKE on the same address.
//   WAKE / WAKE_BITSET (+ _PRIVATE): drain min(val, count) from the
//       waiter slot for uaddr and return that drain count.
//   REQUEUE / CMP_REQUEUE / WAKE_OP: 0 (no kernel-side migration yet).
//   Anything else: -ENOSYS so glibc takes the user-space fallback.
//
// uaddr==0 → -EFAULT for all ops (matches kernel).
static U64 sys_futex64(CPU64* cpu, U64 uaddr, U32 op, U32 val, U64 timeoutAddr, U64 uaddr2, U32 val3) {
    if (uaddr == 0) return (U64)-K_EFAULT;
    if (uaddr & 3) return (U64)-K_EINVAL;

    // Real path: when this CPU64 is driven by a scheduled KThread inside a
    // process (the only way multiple threads exist), delegate to the shared
    // futex table so WAIT actually blocks and WAKE actually wakes siblings.
    if (cpu->thread && cpu->thread->process && cpu->thread->process->memory64) {
        S64 r = cpu->thread->futex64(uaddr, op, val, timeoutAddr, uaddr2, val3);
        if (r == K_FUTEX64_PARKED) {
            // Single-threaded cooperative park: don't write RAX, rewind the
            // SYSCALL on yield so it re-runs after we're woken.
            cpu->reExecuteSyscall = true;
            cpu->yield = true;
            return cpu->reg[X64_RAX].u64; // unused; RAX left intact
        }
        return (U64)r;
    }

    // Threadless fallback for --x64-run-elf / selftest: no real thread to
    // park, so keep the would-block bookkeeping stub (records WAIT-on-match
    // so a later WAKE reports a non-zero count, letting glibc's condvar fast
    // path make forward progress single-threaded).
    U32 baseOp = op & ~(X64_FUTEX_PRIVATE_FLAG | X64_FUTEX_CLOCK_REALTIME);
    switch (baseOp) {
        case X64_FUTEX_WAKE:
        case X64_FUTEX_WAKE_BITSET: {
            auto it = cpu->futexWaiters.find(uaddr);
            if (it == cpu->futexWaiters.end()) return 0;
            U32 toDrain = it->second < val ? it->second : val;
            it->second -= toDrain;
            if (it->second == 0) cpu->futexWaiters.erase(it);
            return toDrain;
        }
        case X64_FUTEX_WAIT:
        case X64_FUTEX_WAIT_BITSET: {
            if (!cpu->memory) return (U64)-K_EFAULT;
            U32 cur = cpu->memory->readd(uaddr);
            if (cur != val) return (U64)-K_EAGAIN;
            cpu->futexWaiters[uaddr] += 1;
            return (U64)-K_EAGAIN;
        }
        case X64_FUTEX_REQUEUE:
        case X64_FUTEX_CMP_REQUEUE: {
            if (!cpu->memory) return (U64)-K_EFAULT;
            if (!uaddr2) return (U64)-K_EFAULT;
            if (uaddr2 & 3) return (U64)-K_EINVAL;
            if ((S32)timeoutAddr < 0) return (U64)-K_EINVAL;
            if (baseOp == X64_FUTEX_CMP_REQUEUE && cpu->memory->readd(uaddr) != val3)
                return (U64)-K_EAGAIN;
            auto source = cpu->futexWaiters.find(uaddr);
            if (source == cpu->futexWaiters.end()) return 0;
            const U32 woken = source->second < val ? source->second : val;
            const U32 remaining = source->second - woken;
            const U32 moved = remaining < (U32)timeoutAddr ? remaining : (U32)timeoutAddr;
            source->second -= woken + moved;
            if (!source->second) cpu->futexWaiters.erase(source);
            if (moved) cpu->futexWaiters[uaddr2] += moved;
            return baseOp == X64_FUTEX_REQUEUE ? woken : woken + moved;
        }
        case X64_FUTEX_WAKE_OP: {
            if (!cpu->memory) return (U64)-K_EFAULT;
            if (!uaddr2) return (U64)-K_EFAULT;
            if (uaddr2 & 3) return (U64)-K_EINVAL;
            const U32 encoded = val3;
            const U32 opCode = (encoded >> 28) & 0xf;
            const U32 cmpCode = (encoded >> 24) & 0xf;
            const bool shiftArg = (opCode & 8) != 0;
            const U32 operation = opCode & 7;
            auto signExtend12 = [](U32 value) -> S32 { return (S32)(value << 20) >> 20; };
            S32 opArg = signExtend12((encoded >> 12) & 0xfff);
            const S32 cmpArg = signExtend12(encoded & 0xfff);
            if (shiftArg) {
                if (opArg < 0 || opArg > 31) return (U64)-K_EINVAL;
                opArg = 1 << opArg;
            }
            if (operation > 4 || cmpCode > 5) return (U64)-K_EINVAL;
            const U32 oldValue = cpu->memory->readd(uaddr2);
            U32 newValue = oldValue;
            switch (operation) {
                case 0: newValue = (U32)opArg; break; // SET
                case 1: newValue += (U32)opArg; break; // ADD
                case 2: newValue |= (U32)opArg; break; // OR
                case 3: newValue &= ~(U32)opArg; break; // ANDN
                case 4: newValue ^= (U32)opArg; break; // XOR
            }
            cpu->memory->writed(uaddr2, newValue);
            const S32 signedOld = (S32)oldValue;
            bool compare = false;
            switch (cmpCode) {
                case 0: compare = signedOld == cmpArg; break;
                case 1: compare = signedOld != cmpArg; break;
                case 2: compare = signedOld < cmpArg; break;
                case 3: compare = signedOld <= cmpArg; break;
                case 4: compare = signedOld > cmpArg; break;
                case 5: compare = signedOld >= cmpArg; break;
            }
            auto wake = [&](U64 address, U32 maximum) -> U32 {
                auto it = cpu->futexWaiters.find(address);
                if (it == cpu->futexWaiters.end()) return 0;
                const U32 count = it->second < maximum ? it->second : maximum;
                it->second -= count;
                if (!it->second) cpu->futexWaiters.erase(it);
                return count;
            };
            const U32 first = wake(uaddr, val);
            const U32 second = compare ? wake(uaddr2, (U32)timeoutAddr) : 0;
            return first + second;
        }
        case 6: // FUTEX_LOCK_PI
        case 7: // FUTEX_UNLOCK_PI
        case 8: // FUTEX_TRYLOCK_PI
        case 11: // FUTEX_WAIT_REQUEUE_PI
        case 12: // FUTEX_CMP_REQUEUE_PI
        default: {
            static std::mutex reportMutex;
            static std::unordered_set<U32> reportedOps;
            const U32 cmd = op & ~(X64_FUTEX_PRIVATE_FLAG | X64_FUTEX_CLOCK_REALTIME);
            bool first;
            {
                std::lock_guard<std::mutex> lock(reportMutex);
                first = reportedOps.insert(cmd).second;
            }
            if (first) klog_fmt("ksyscall64: unsupported threadless futex operation %u (op=0x%x); returned -ENOSYS", cmd, op);
            return (U64)-K_ENOSYS;
        }
    }
}

// rt_sigtimedwait(set, info, timeout, sigsetsize) — block till a signal in
// `set` arrives or `timeout` elapses. With no delivery implemented, the
// only correct non-hanging answer is -EAGAIN (timeout). Validate sigsetsize
// for the standard 8-byte mask first.
static U64 sys_rt_sigtimedwait64(CPU64* /*cpu*/, U64 setPtr, U64 /*infoPtr*/,
                                  U64 /*timeoutPtr*/, U64 sigsetsize) {
    if (sigsetsize != 8) return (U64)-K_EINVAL;
    if (setPtr == 0) return (U64)-K_EFAULT;
    // No signals pending; timeout result.
    return (U64)-K_EAGAIN;
}

// rt_sigsuspend(mask, sigsetsize) — replace mask, sleep till signal, restore.
// With no delivery, returning -EINTR forces glibc to retry the surrounding
// loop (correct for poll/select-on-signal idioms).
static U64 sys_rt_sigsuspend64(CPU64* /*cpu*/, U64 maskPtr, U64 sigsetsize) {
    if (sigsetsize != 8) return (U64)-K_EINVAL;
    if (maskPtr == 0) return (U64)-K_EFAULT;
    return (U64)-K_EINTR;
}

// rt_sigpending(set, sigsetsize) — write currently-pending signal mask.
// Nothing is ever pending in our world; write zeros and succeed.
static U64 sys_rt_sigpending64(CPU64* cpu, U64 setPtr, U64 sigsetsize) {
    if (sigsetsize != 8) return (U64)-K_EINVAL;
    if (setPtr == 0) return (U64)-K_EFAULT;
    if (!cpu->memory) return (U64)-K_EFAULT;
    cpu->memory->writeq(setPtr, 0);
    return 0;
}

// rt_sigaction(2) — storage-only round-trip. We do not yet deliver signals
// to guest handlers (that needs the signal-frame builder of Milestone B),
// but glibc's startup *registers* SIGFPE/SIGSEGV/SIGPIPE handlers very
// early and later queries them; previously this was a bald no-op so the
// second sigaction(SIG, NULL, &old) call always reported SIG_DFL, which
// confuses libpthread's "did the user install a handler?" check.
//
// x86-64 `struct kernel_sigaction` layout (32 bytes):
//   off  0: sa_handler  (8)
//   off  8: sa_flags    (8)
//   off 16: sa_restorer (8)
//   off 24: sa_mask     (8, = full sigset_t on x86-64)
//
// Signal numbers 1..64 are valid; SIGKILL(9) and SIGSTOP(19) can be queried
// but cannot have their handlers changed — we accept the read and silently
// drop the write, matching kernel behaviour.
static U64 sys_rt_sigaction64(CPU64* cpu, U64 sig, U64 actPtr, U64 oldActPtr,
                              U64 sigsetsize) {
    if (sig < 1 || sig > 64) return (U64)-K_EINVAL;
    if (sigsetsize != 8) return (U64)-K_EINVAL; // x86-64 sigset_t is 8 bytes
    if (!cpu->memory) return (U64)-K_EFAULT;

    CPU64::SigAction& slot = cpu->sigActions[sig];

    if (oldActPtr) {
        cpu->memory->writeq(oldActPtr + 0,  slot.installed ? slot.handler  : 0);
        cpu->memory->writeq(oldActPtr + 8,  slot.installed ? slot.flags    : 0);
        cpu->memory->writeq(oldActPtr + 16, slot.installed ? slot.restorer : 0);
        cpu->memory->writeq(oldActPtr + 24, slot.installed ? slot.mask     : 0);
    }

    if (actPtr) {
        if (sig == 9 || sig == 19) {
            // SIGKILL / SIGSTOP: read accepted, write ignored — matches Linux.
            return 0;
        }
        slot.handler  = cpu->memory->readq(actPtr + 0);
        slot.flags    = cpu->memory->readq(actPtr + 8);
        slot.restorer = cpu->memory->readq(actPtr + 16);
        slot.mask     = cpu->memory->readq(actPtr + 24);
        slot.installed = true;
    }
    return 0;
}

// rt_sigprocmask(2) — storage-only round-trip, paired with rt_sigaction
// above. how=SIG_BLOCK(0)/SIG_UNBLOCK(1)/SIG_SETMASK(2). v1 enforces nothing
// at delivery time (no delivery yet) but lets pthread_sigmask round-trip
// without losing state, which libpthread queries during thread init.
#define X64_SIG_BLOCK   0
#define X64_SIG_UNBLOCK 1
#define X64_SIG_SETMASK 2

static U64 sys_rt_sigprocmask64(CPU64* cpu, U64 how, U64 setPtr, U64 oldSetPtr,
                                U64 sigsetsize) {
    if (sigsetsize != 8) return (U64)-K_EINVAL;
    if (!cpu->memory) return (U64)-K_EFAULT;

    if (oldSetPtr) {
        cpu->memory->writeq(oldSetPtr, cpu->sigMask);
    }
    if (setPtr) {
        U64 incoming = cpu->memory->readq(setPtr);
        // SIGKILL(9) and SIGSTOP(19) can never be blocked — strip them
        // from any incoming mask to match kernel behaviour.
        U64 kernelStrip = (1ULL << (9 - 1)) | (1ULL << (19 - 1));
        incoming &= ~kernelStrip;
        switch (how) {
            case X64_SIG_BLOCK:   cpu->sigMask |=  incoming; break;
            case X64_SIG_UNBLOCK: cpu->sigMask &= ~incoming; break;
            case X64_SIG_SETMASK: cpu->sigMask  =  incoming; break;
            default: return (U64)-K_EINVAL;
        }
    }
    return 0;
}

// sigaltstack(2) — storage-only round-trip, completes the sig{action,
// procmask, altstack} trio. Layout of stack_t on x86-64 (24 bytes):
//   off  0: ss_sp    (8)
//   off  8: ss_flags (4)
//   off 12: pad      (4)
//   off 16: ss_size  (8)
//
// Kernel rules we honour here:
//   - If oldss != NULL, write the current state first.
//   - If ss != NULL with SS_DISABLE(2): clear the registration.
//   - If ss != NULL otherwise: ss_flags must be 0 (or SS_AUTODISARM=0x80000000)
//     and ss_size must be >= MINSIGSTKSZ (~2048). Reject otherwise.
//   - Cannot change altstack while currently executing on it (SS_ONSTACK
//     flag set). We don't track that yet, so the check is skipped.
#define X64_SS_ONSTACK     1
#define X64_SS_DISABLE     2
#define X64_SS_AUTODISARM  0x80000000u
#define X64_MINSIGSTKSZ    2048

static U64 sys_sigaltstack64(CPU64* cpu, U64 ssPtr, U64 oldSsPtr) {
    if (!cpu->memory) return (U64)-K_EFAULT;

    if (oldSsPtr) {
        cpu->memory->writeq(oldSsPtr + 0,  cpu->sigAltStack.ssSp);
        cpu->memory->writed(oldSsPtr + 8,  cpu->sigAltStack.ssFlags);
        cpu->memory->writed(oldSsPtr + 12, 0);
        cpu->memory->writeq(oldSsPtr + 16, cpu->sigAltStack.ssSize);
    }

    if (ssPtr) {
        U64 sp    = cpu->memory->readq(ssPtr + 0);
        U32 flags = cpu->memory->readd(ssPtr + 8);
        U64 size  = cpu->memory->readq(ssPtr + 16);

        if (flags & X64_SS_DISABLE) {
            cpu->sigAltStack.ssSp    = 0;
            cpu->sigAltStack.ssFlags = X64_SS_DISABLE;
            cpu->sigAltStack.ssSize  = 0;
        } else {
            U32 allowed = X64_SS_AUTODISARM;
            if (flags & ~allowed) return (U64)-K_EINVAL;
            if (size < X64_MINSIGSTKSZ) return (U64)-K_ENOMEM;
            cpu->sigAltStack.ssSp    = sp;
            cpu->sigAltStack.ssFlags = flags;
            cpu->sigAltStack.ssSize  = size;
        }
    }
    return 0;
}

// sched_getaffinity(2): glibc + libgomp probe this very early to decide thread
// pool sizes. We expose exactly one CPU. The Linux signature is unusual:
//   ssize_t sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
// On success returns the number of bytes the kernel actually wrote (clamped
// to cpusetsize, but must be a multiple of sizeof(long)=8). cpusetsize must
// be a multiple of sizeof(long) and >= 8.
//
// We write 8 bytes (one U64 with bit 0 set) and return 8. Userspace then
// counts the bits to get nproc, which is exactly what we want it to see.
static U64 sys_sched_getaffinity64(CPU64* cpu, U64 pid, U64 cpusetsize, U64 maskPtr) {
    (void)pid; // ignore — we model one process
    if (cpusetsize == 0 || (cpusetsize & 7)) return (U64)-K_EINVAL;
    if (!maskPtr) return (U64)-K_EFAULT;
    if (!cpu->memory) return (U64)-K_EFAULT;

    // Write 1 in the low qword (CPU 0 is in our affinity set), zero the rest
    // up to cpusetsize. Userspace inspects only the bytes the kernel wrote
    // (which is our return value), so 8 bytes is enough.
    cpu->memory->writeq(maskPtr, 1);
    for (U64 off = 8; off < cpusetsize && off < 1024; off += 8) {
        cpu->memory->writeq(maskPtr + off, 0);
    }
    return 8;
}

// sched_setaffinity(2): silently accept any mask — we don't actually move
// the thread anywhere, but reject obviously bogus calls.
static U64 sys_sched_setaffinity64(CPU64* cpu, U64 pid, U64 cpusetsize, U64 maskPtr) {
    (void)cpu; (void)pid; (void)maskPtr;
    if (cpusetsize == 0 || (cpusetsize & 7)) return (U64)-K_EINVAL;
    return 0;
}

// statfs(2) / fstatfs(2) — fixed-shape stub. Layout of x86-64 struct statfs
// (120 bytes, all fields long-sized):
//   off  0: f_type        off 56: f_fsid (8)
//   off  8: f_bsize       off 64: f_namelen
//   off 16: f_blocks      off 72: f_frsize
//   off 24: f_bfree       off 80: f_flags
//   off 32: f_bavail      off 88..119: f_spare[4]
//   off 40: f_files
//   off 48: f_ffree
//
// glibc's dynamic loader probes statfs("/proc"), the system's ld.so.cache
// directory, etc. The load-bearing fields are f_type (to know "is this
// tmpfs/proc/etc?"), f_bsize (cache alignment), and f_namelen (path
// canonicalisation). We claim tmpfs (0x01021994) with 4096-byte blocks
// and 255-byte filenames, zero free/total — glibc tolerates the lie.
#define X64_TMPFS_MAGIC 0x01021994

static U64 sys_statfs64_common(CPU64* cpu, U64 bufPtr) {
    if (!bufPtr) return (U64)-K_EFAULT;
    if (!cpu->memory) return (U64)-K_EFAULT;
    cpu->memory->writeq(bufPtr +  0, X64_TMPFS_MAGIC); // f_type
    cpu->memory->writeq(bufPtr +  8, 4096);            // f_bsize
    cpu->memory->writeq(bufPtr + 16, 0);               // f_blocks
    cpu->memory->writeq(bufPtr + 24, 0);               // f_bfree
    cpu->memory->writeq(bufPtr + 32, 0);               // f_bavail
    cpu->memory->writeq(bufPtr + 40, 0);               // f_files
    cpu->memory->writeq(bufPtr + 48, 0);               // f_ffree
    cpu->memory->writeq(bufPtr + 56, 0);               // f_fsid
    cpu->memory->writeq(bufPtr + 64, 255);             // f_namelen
    cpu->memory->writeq(bufPtr + 72, 4096);            // f_frsize
    cpu->memory->writeq(bufPtr + 80, 0);               // f_flags
    cpu->memory->writeq(bufPtr + 88, 0);
    cpu->memory->writeq(bufPtr + 96, 0);
    cpu->memory->writeq(bufPtr + 104, 0);
    cpu->memory->writeq(bufPtr + 112, 0);
    return 0;
}

// ============================================================================
// Signal-frame layout (Linux x86-64 rt_sigframe / ucontext_t).
// ============================================================================
//
// When the kernel delivers a signal it pushes this structure on the user
// stack and sets RIP = handler, RDI = signo, RSI = &siginfo, RDX = &uctx.
// rt_sigreturn(15) reverses it: read the saved cpu state out of the frame
// at the current RSP and restore.
//
// We use the Linux-glibc layout so any handler compiled for x86-64 Linux
// can read it directly. Field offsets and sizes:
//
//   off  +0   rt_sigframe header                       (8 bytes pretty + pad)
//             — actually just the saved restorer addr (8 bytes), kernel
//               relies on the handler to RET into it; we always set RIP
//               from the saved RIP in mcontext on sigreturn so the header
//               is informational only.
//   off  +8   siginfo_t (128 bytes)
//   off +136  ucontext_t
//      +0     uc_flags          (8)
//      +8     uc_link           (8)
//      +16    uc_stack          (24: ss_sp, ss_flags+pad, ss_size)
//      +40    uc_mcontext.gregs[23]  — see gregs index table below
//                                       (23 * 8 = 184 bytes)
//      +224   uc_mcontext.fpregs (8 — pointer; we set 0 → no FPU restore)
//      +232   uc_mcontext.__reserved[8]  (64 bytes — zeroed)
//      +296   uc_sigmask        (8 — single qword, rest of 128-byte sigset
//                                    region is zero-padded)
//      +424   __fpregs_mem      (we don't write this for now)
//
// Total frame size we allocate on the stack: 16-aligned, round up to 1024.
// (Linux's actual frame is ~600 bytes with fpregs_mem; we shrink because
// we never populate fpregs.)
//
// gregs[] index, matching <sys/ucontext.h> REG_* enum:
//   0:R8   1:R9   2:R10  3:R11  4:R12  5:R13  6:R14  7:R15
//   8:RDI  9:RSI 10:RBP 11:RBX 12:RDX 13:RAX 14:RCX 15:RSP
//  16:RIP 17:EFL 18:CSGSFS 19:ERR 20:TRAPNO 21:OLDMASK 22:CR2
#define X64_SIGFRAME_SIZE          1024
#define X64_UCONTEXT_OFF_IN_FRAME  136   // after siginfo
#define X64_MCONTEXT_OFF_IN_UCTX    40
#define X64_GREGS_OFF_IN_UCTX       X64_MCONTEXT_OFF_IN_UCTX
#define X64_SIGMASK_OFF_IN_UCTX    296

// Indices into mcontext.gregs[23].
enum X64Greg {
    X64_GREG_R8 = 0, X64_GREG_R9, X64_GREG_R10, X64_GREG_R11,
    X64_GREG_R12, X64_GREG_R13, X64_GREG_R14, X64_GREG_R15,
    X64_GREG_RDI, X64_GREG_RSI, X64_GREG_RBP, X64_GREG_RBX,
    X64_GREG_RDX, X64_GREG_RAX, X64_GREG_RCX, X64_GREG_RSP,
    X64_GREG_RIP, X64_GREG_EFL, X64_GREG_CSGSFS, X64_GREG_ERR,
    X64_GREG_TRAPNO, X64_GREG_OLDMASK, X64_GREG_CR2,
};

// Build a signal frame on the stack at `framePtr` capturing cpu's full
// gpr/rflags/rip/sigmask state. Caller has already aligned the stack and
// reserved X64_SIGFRAME_SIZE bytes. Returns the address of the ucontext_t
// within the frame (handler receives this as RDX).
static U64 buildSignalFrame(CPU64* cpu, U64 framePtr) {
    // Zero the entire frame first (siginfo, padding, gaps).
    for (U64 i = 0; i < X64_SIGFRAME_SIZE; i += 8) {
        cpu->memory->writeq(framePtr + i, 0);
    }
    U64 uctxPtr   = framePtr + X64_UCONTEXT_OFF_IN_FRAME;
    U64 mctxPtr   = uctxPtr  + X64_MCONTEXT_OFF_IN_UCTX;
    U64 gregsPtr  = mctxPtr; // gregs starts at mcontext
    // uc_stack: copy the registered altstack so the handler can re-arm if needed.
    cpu->memory->writeq(uctxPtr + 16, cpu->sigAltStack.ssSp);
    cpu->memory->writed(uctxPtr + 24, cpu->sigAltStack.ssFlags);
    cpu->memory->writeq(uctxPtr + 32, cpu->sigAltStack.ssSize);
    // gregs[]
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_R8,  cpu->reg[X64_R8].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_R9,  cpu->reg[X64_R9].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_R10, cpu->reg[X64_R10].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_R11, cpu->reg[X64_R11].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_R12, cpu->reg[X64_R12].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_R13, cpu->reg[X64_R13].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_R14, cpu->reg[X64_R14].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_R15, cpu->reg[X64_R15].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_RDI, cpu->reg[X64_RDI].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_RSI, cpu->reg[X64_RSI].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_RBP, cpu->reg[X64_RBP].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_RBX, cpu->reg[X64_RBX].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_RDX, cpu->reg[X64_RDX].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_RAX, cpu->reg[X64_RAX].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_RCX, cpu->reg[X64_RCX].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_RSP, cpu->reg[X64_RSP].u64);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_RIP, cpu->rip);
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_EFL, (U64)cpu->rflags);
    // CSGSFS: low 16 = CS (we don't model segments → 0x33 USER_CS),
    //         next 16 = GS, next 16 = FS, top 16 = ss. Approximate.
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_CSGSFS,
                        (U64)0x33 | ((U64)0x2B << 48));
    // ERR/TRAPNO/CR2 = 0 (no fault triggered this delivery). A hardware-fault
    // delivery overwrites these afterwards via the fault-info it has.
    // OLDMASK = caller's sigmask before this delivery (we set it to current)
    cpu->memory->writeq(gregsPtr + 8 * X64_GREG_OLDMASK, cpu->sigMask);
    // fpregs pointer = 0 (we don't snapshot XMM/x87 here yet — the next
    // increment after delivery wiring lands)
    cpu->memory->writeq(mctxPtr + 184, 0);
    // uc_sigmask
    cpu->memory->writeq(uctxPtr + X64_SIGMASK_OFF_IN_UCTX, cpu->sigMask);
    return uctxPtr;
}

// Restore cpu state from a signal frame whose ucontext_t lives at the
// current RSP. The kernel's invariant after rt_sigreturn is that RSP
// points one byte *past* the saved-rsp slot (i.e. the frame has been
// popped); we read everything we need, then write cpu->rip and cpu->reg
// values en masse. mcontext lives at RSP + (X64_MCONTEXT_OFF_IN_UCTX).
//
// Returns 0 on success, negative errno otherwise. The kernel actually
// returns the saved RAX as the syscall result so the user code sees the
// pre-signal RAX restored; we mirror that by writing RAX last and
// signalling "skip the normal RAX-as-return-value path" via a special
// sentinel (the caller checks cpu->yield).
static U64 restoreSignalFrame(CPU64* cpu, U64 uctxPtr) {
    U64 mctxPtr   = uctxPtr + X64_MCONTEXT_OFF_IN_UCTX;
    U64 gregsPtr  = mctxPtr;
    cpu->reg[X64_R8].u64  = cpu->memory->readq(gregsPtr + 8 * X64_GREG_R8);
    cpu->reg[X64_R9].u64  = cpu->memory->readq(gregsPtr + 8 * X64_GREG_R9);
    cpu->reg[X64_R10].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_R10);
    cpu->reg[X64_R11].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_R11);
    cpu->reg[X64_R12].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_R12);
    cpu->reg[X64_R13].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_R13);
    cpu->reg[X64_R14].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_R14);
    cpu->reg[X64_R15].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_R15);
    cpu->reg[X64_RDI].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_RDI);
    cpu->reg[X64_RSI].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_RSI);
    cpu->reg[X64_RBP].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_RBP);
    cpu->reg[X64_RBX].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_RBX);
    cpu->reg[X64_RDX].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_RDX);
    cpu->reg[X64_RCX].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_RCX);
    cpu->reg[X64_RSP].u64 = cpu->memory->readq(gregsPtr + 8 * X64_GREG_RSP);
    cpu->rip              = cpu->memory->readq(gregsPtr + 8 * X64_GREG_RIP);
    cpu->rflags           = (U32)cpu->memory->readq(gregsPtr + 8 * X64_GREG_EFL);
    cpu->sigMask          = cpu->memory->readq(uctxPtr   + X64_SIGMASK_OFF_IN_UCTX);
    // RAX is restored last — the kernel returns RAX as the syscall return
    // value so the user-visible RAX is the *pre-signal* value, not whatever
    // rt_sigreturn would compute.
    U64 savedRax = cpu->memory->readq(gregsPtr + 8 * X64_GREG_RAX);
    return savedRax;
}

// SA_ONSTACK = 0x08000000 in glibc's <bits/sigaction.h>. When set, the
// handler runs on the registered sigaltstack instead of the user stack.
#define X64_SA_ONSTACK   0x08000000

// Synchronously deliver `sig` to `cpu` (the same thread). Models the
// kernel's "queue a signal that's pending immediately" path for the
// single-thread case — i.e. raise(), abort(), pthread_kill(self,...).
//
// Caller has already validated the signal number and confirmed that
// cpu->sigActions[sig].installed && handler is a real function pointer
// (not SIG_DFL/SIG_IGN).
//
// Mechanics:
//   1. Pick the frame stack: altstack if SA_ONSTACK set and altstack is
//      live, otherwise current RSP minus a redzone (128 bytes per SysV).
//   2. Round down to 16-byte alignment, subtract frame size.
//   3. Build the ucontext_t at frame_base via buildSignalFrame.
//   4. Set cpu->reg[X64_RSP] = frame_base, RIP = handler, RDI = sig,
//      RSI = 0 (no siginfo), RDX = &uctx, R10 = 0, R8/R9 = 0.
//   5. Mask the signal in cpu->sigMask for the duration of handler
//      execution (kernel adds sig to mask unless SA_NODEFER set).
//   6. The handler ends with `ret`-to-restorer or `syscall(rt_sigreturn)`,
//      which (a) hits our rt_sigreturn case → restoreSignalFrame → state
//      restored, including sigMask.
//
// Returns true on successful delivery (handler will run next), false if
// the handler slot is not installed / is SIG_DFL/SIG_IGN.
static bool deliverSignalSync(CPU64* cpu, U32 sig) {
    if (sig < 1 || sig > 64) return false;
    CPU64::SigAction& sa = cpu->sigActions[sig];
    if (!sa.installed) return false;
    if (sa.handler == 0 /*SIG_DFL*/ || sa.handler == 1 /*SIG_IGN*/) return false;

    // Pick stack: SA_ONSTACK requires a non-DISABLED altstack.
    U64 baseSp;
    if ((sa.flags & X64_SA_ONSTACK) && cpu->sigAltStack.ssSp != 0 &&
        (cpu->sigAltStack.ssFlags & 2 /*SS_DISABLE*/) == 0) {
        baseSp = cpu->sigAltStack.ssSp + cpu->sigAltStack.ssSize;
    } else {
        baseSp = cpu->reg[X64_RSP].u64 - 128; // red zone
    }
    // 16-align then reserve the frame.
    U64 frameBase = (baseSp - X64_SIGFRAME_SIZE) & ~(U64)15;

    U64 uctxPtr = buildSignalFrame(cpu, frameBase);

    // x86-64 signal-frame ABI: the kernel pushes the restorer address *below*
    // the ucontext so the handler's terminating `ret` pops it and jumps to
    // the restorer. The restorer then issues `syscall rt_sigreturn` with RSP
    // pointing at the ucontext (== uctxPtr). Without this, the handler's
    // `ret` pops the first qword of ucontext as garbage and crashes.
    U64 retSlot = uctxPtr - 8;
    cpu->memory->writeq(retSlot, sa.restorer);

    // Mask the signal during handler execution (unless SA_NODEFER=0x40000000).
    if ((sa.flags & 0x40000000) == 0) {
        cpu->sigMask |= (1ULL << (sig - 1));
    }
    cpu->sigMask |= sa.mask;

    // Hand off to the handler. RSP points at restorer_addr so `ret` pops it
    // and leaves RSP = uctxPtr, which is what rt_sigreturn expects.
    cpu->reg[X64_RSP].setU64(retSlot);
    cpu->reg[X64_RDI].setU64(sig);      // arg1: signal number
    cpu->reg[X64_RSI].setU64(0);        // arg2: siginfo (we don't synth one)
    cpu->reg[X64_RDX].setU64(uctxPtr);  // arg3: ucontext pointer
    cpu->rip = sa.handler;

    return true;
}

// Deliver one pending, unmasked, installed async signal queued on this thread by
// a cross-thread tkill/tgkill. Called by the scheduler at the start of the
// thread's slice (the thread is parked, so building a frame on its CPU64 is
// safe). At most one per call — the handler runs, and any further pending
// signals are taken on subsequent slices (after rt_sigreturn unmasks). Returns
// true if a signal was delivered.
bool CPU64::deliverPendingSignals() {
    if (!this->thread) return false;
    U64 pending;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->thread->pendingSignalsMutex);
        pending = this->thread->pendingSignals & ~this->sigMask;
    }
    if (!pending) return false;
    for (U32 sig = 1; sig <= 64; sig++) {
        U64 bit = 1ULL << (sig - 1);
        if (!(pending & bit)) continue;
        // Only consume the bit if we can actually deliver it (a handler is
        // installed). If it's SIG_DFL/IGN/uninstalled, leave it pending — for
        // the wineserver SIGUSR1-APC case the handler is always installed by the
        // time wineserver signals the client; an undeliverable signal shouldn't
        // be silently dropped here.
        if (deliverSignalSync(this, sig)) {
            BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->thread->pendingSignalsMutex);
            this->thread->pendingSignals &= ~bit;
            return true;
        }
    }
    return false;
}

// Blocked-syscall variant of deliverPendingSignals (see cpu64.h). The thread is
// inside a SYSCALL (cpu->rip is already past the instruction, syscallRip is the
// instruction itself, RAX still holds the syscall number). Wine suspends/queries
// a thread (SuspendThread, GetThreadContext, APC nudges) by tgkill(SIGUSR1) and
// expects the signal to break the thread out of whatever server wait it is in;
// without this a thread parked in a wineserver socket read never runs its
// handler and the requester (e.g. a Boehm GC stop-the-world) waits forever.
bool CPU64::interruptSyscallWithSignal(bool allowRestart) {
    if (!this->thread) return false;
    U64 pending;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->thread->pendingSignalsMutex);
        pending = this->thread->pendingSignals & ~this->sigMask;
    }
    if (!pending) return false;
    const U64 savedRip = this->rip;
    const U64 savedRax = this->reg[X64_RAX].u64;
    for (U32 sig = 1; sig <= 64; sig++) {
        U64 bit = 1ULL << (sig - 1);
        if (!(pending & bit)) continue;
        const SigAction& sa = this->sigActions[sig];
        if (!sa.installed || sa.handler == 0 || sa.handler == 1) continue;
        if (allowRestart && (sa.flags & 0x10000000 /*SA_RESTART*/)) {
            // Re-run the SYSCALL after the handler returns: same RAX (number),
            // same argument registers; RCX/R11 are re-clobbered by SYSCALL.
            this->rip = this->syscallRip;
        } else {
            this->reg[X64_RAX].setU64((U64)(S64)-K_EINTR);
        }
        if (deliverSignalSync(this, sig)) {
            BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->thread->pendingSignalsMutex);
            this->thread->pendingSignals &= ~bit;
            return true;
        }
        this->rip = savedRip;
        this->reg[X64_RAX].setU64(savedRax);
    }
    return false;
}

// Synchronously deliver a hardware-trap-derived signal (SIGFPE on #DE,
// SIGSEGV on a page fault, ...) at the faulting instruction. Unlike
// deliverSignalSync (used for raise()/tgkill self), this synthesizes a real
// siginfo and fills the mcontext TRAPNO/ERR/CR2 so wine's (and glibc's)
// signal handlers see a genuine fault — wine's setup_raise_exception maps
// SIGFPE + si_code → EXCEPTION_INT_DIVIDE_BY_ZERO etc, and uses the ucontext
// RIP as the exception address. cpu->rip must still point at the faulting
// instruction when this is called (so the captured RIP is the fault site).
bool CPU64::raiseSyncFault(U32 sig, U32 trapNo, S32 siCode, U64 faultAddr,
                           U32 errorCode) {
    if (sig < 1 || sig > 64) return false;
    SigAction& sa = this->sigActions[sig];
    if (!sa.installed) return false;
    if (sa.handler == 0 /*SIG_DFL*/ || sa.handler == 1 /*SIG_IGN*/) return false;

    U64 baseSp;
    if ((sa.flags & X64_SA_ONSTACK) && this->sigAltStack.ssSp != 0 &&
        (this->sigAltStack.ssFlags & 2 /*SS_DISABLE*/) == 0) {
        baseSp = this->sigAltStack.ssSp + this->sigAltStack.ssSize;
    } else {
        baseSp = this->reg[X64_RSP].u64 - 128; // red zone
    }
    U64 frameBase = (baseSp - X64_SIGFRAME_SIZE) & ~(U64)15;

    U64 uctxPtr = buildSignalFrame(this, frameBase);

    // Fill the fault-specific mcontext slots the generic builder leaves zero.
    U64 gregsPtr = uctxPtr + X64_MCONTEXT_OFF_IN_UCTX;
    this->memory->writeq(gregsPtr + 8 * X64_GREG_TRAPNO, (U64)trapNo);
    this->memory->writeq(gregsPtr + 8 * X64_GREG_ERR, errorCode);
    this->memory->writeq(gregsPtr + 8 * X64_GREG_CR2, faultAddr);

    // Synthesize a siginfo_t at the start of the frame (the siginfo region sits
    // before the ucontext at X64_UCONTEXT_OFF_IN_FRAME). Layout (x86-64):
    //   int si_signo @0; int si_errno @4; int si_code @8; then the union,
    //   where the SIGFPE/SIGSEGV variant places void* si_addr @16.
    U64 siPtr = frameBase;
    this->memory->writed(siPtr + 0, sig);
    this->memory->writed(siPtr + 4, 0);
    this->memory->writed(siPtr + 8, (U32)siCode);
    this->memory->writeq(siPtr + 16, faultAddr);

    U64 retSlot = uctxPtr - 8;
    this->memory->writeq(retSlot, sa.restorer);

    if ((sa.flags & 0x40000000) == 0) {
        this->sigMask |= (1ULL << (sig - 1));
    }
    this->sigMask |= sa.mask;

    this->reg[X64_RSP].setU64(retSlot);
    this->reg[X64_RDI].setU64(sig);        // arg1: signal number
    this->reg[X64_RSI].setU64(siPtr);      // arg2: siginfo*
    this->reg[X64_RDX].setU64(uctxPtr);    // arg3: ucontext*
    this->rip = sa.handler;
    return true;
}

// --- 64-bit socket address bounce -------------------------------------------
// The socket object layer (kbind/kconnect/kaccept/kgetsockname and their
// KSocketObject methods) reads the guest sockaddr through `thread->memory` —
// the 32-bit KMemory. For a 64-bit process that memory is allocated but unused
// (the real address space is memory64), so the sockaddr the guest passed lives
// in memory64, not where the object layer looks. sockaddr_un/_in are pointer-
// free (identical layout 32/64), so rather than duplicate every object method
// we BOUNCE: copy the sockaddr bytes out of memory64 into a small scratch page
// in the 32-bit KMemory, then call the existing 32-bit k* socket helper with
// that scratch address. This reuses all the bind/connect/accept/handshake logic
// unchanged. The scratch page is mmap'd once per process (lazily) and reused.
//
// Returns the 32-bit scratch address holding `len` bytes copied from `src64`,
// or 0 on failure. `outLenAddr` (optional) gets a 4-byte slot holding `len`
// for the helpers that take a pointer-to-socklen_t.
// The scratch must be PER THREAD, not per process: each KProcess has its own
// 32-bit KMemory (so an address mmap'd in one process is unmapped in another —
// the forked wine64 client vs. wineserver), AND sibling THREADS of one process
// share that KMemory but run concurrently. A single per-process scratch is
// clobbered when two threads do socket calls at once (services.exe spawns a
// clone3 worker thread that hammers wineserver alongside the main thread) —
// the result is a malformed request that corrupts wineserver's heap ("unsorted
// double linked list"). So cache (threadId -> addr) and mmap a fresh region the
// first time each thread needs one. KThread::id is unique and stable.
static std::unordered_map<U32, U32> g_socketScratchByThread;
static BOXEDWINE_MUTEX g_scratchMutex;
static U32 bounceSockaddrTo32(CPU64* cpu, U64 src64, U32 len, U32* outLenAddr) {
    KThread* thread = cpu->thread;
    if (!thread || !thread->memory || !thread->process) return 0;
    if (len > 256) len = 256; // sockaddr_un is 110 bytes; cap defensively
    U32 addr;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(g_scratchMutex);
        auto it = g_socketScratchByThread.find(thread->id);
        if (it != g_socketScratchByThread.end()) {
            addr = it->second;
        } else {
            // One page is plenty for a sockaddr + a socklen_t slot.
            addr = thread->memory->mmap(thread, 0, K_PAGE_SIZE,
                K_PROT_READ | K_PROT_WRITE, K_MAP_ANONYMOUS | K_MAP_PRIVATE, -1, 0);
            if (!addr) return 0;
            g_socketScratchByThread[thread->id] = addr;
        }
    }
    if (src64 && len) {
        U8 tmp[256];
        cpu->memory->memcpyFromGuest(tmp, src64, len);
        thread->memory->memcpy(addr, tmp, len);
    }
    if (outLenAddr) {
        U32 lenSlot = addr + 256;
        thread->memory->writed(lenSlot, len);
        *outLenAddr = lenSlot;
    }
    return addr;
}

// Ensure a multi-page scratch region in the 64-bit process's (otherwise unused)
// 32-bit KMemory, big enough for a synthesized 32-bit msghdr + iovec + cmsg +
// a data buffer. Same per-process caching rationale as bounceSockaddrTo32.
// Layout within the region:
//   +0      : 32-bit msghdr (28 bytes: name,namelen,iov,iovlen,control,controllen,flags)
//   +32     : one 32-bit iovec {base,len}
//   +64     : cmsg area (up to a few SCM_RIGHTS fds)
//   +256    : socklen/len scratch
//   +512 .. : data buffer (region size - 512)
#define MSG_SCRATCH_BYTES   (64 * 1024)
#define MSG_SCRATCH_HDR     0
#define MSG_SCRATCH_IOV     32
#define MSG_SCRATCH_CMSG    64
#define MSG_SCRATCH_DATA    512
// Per-THREAD (see bounceSockaddrTo32's note): a per-process msg scratch is
// corrupted when sibling threads sendmsg/recvmsg concurrently.
static std::unordered_map<U32, U32> g_msgScratchByThread;
U32 msgScratch(KThread* thread) {
    if (!thread || !thread->memory || !thread->process) return 0;
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(g_scratchMutex);
    auto it = g_msgScratchByThread.find(thread->id);
    if (it != g_msgScratchByThread.end()) return it->second;
    U32 addr = thread->memory->mmap(thread, 0, MSG_SCRATCH_BYTES,
        K_PROT_READ | K_PROT_WRITE, K_MAP_ANONYMOUS | K_MAP_PRIVATE, -1, 0);
    if (!addr) return 0;
    g_msgScratchByThread[thread->id] = addr;
    return addr;
}

// Invalidate the cached per-thread 32-bit IPC scratch addresses for a thread.
// MUST be called whenever that thread's 32-bit KMemory is wiped — i.e. on
// execve (execvReset frees the process's 32-bit mappings, including the scratch
// page). Without this the cache hands back a stale address that is no longer
// mapped, and the next sockaddr/msg bounce faults in KMemory::performOnMemory
// ("failed to get ram"). This surfaced once the persistent-session spawn path
// ran a guest through preloader->wine64 execs on a non-main worker: the scratch
// was mmap'd before an execve and reused after it. See KProcess::execve.
void bw64InvalidateScratchForThread(U32 threadId) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(g_scratchMutex);
    g_socketScratchByThread.erase(threadId);
    g_msgScratchByThread.erase(threadId);
}

// 64-bit struct offsets (x86-64):
//   msghdr {name(0); namelen(8); iov(16); iovlen(24); control(32); controllen(40); flags(48)}  =56
//   iovec  {base(0); len(8)}                                                                    =16
//   cmsghdr{len(0,size_t); level(8,int); type(12,int); data(16)}
// 32-bit (what the socket object expects):
//   msghdr {name(0); namelen(4); iov(8); iovlen(12); control(16); controllen(20); flags(24)}
//   iovec  {base(0); len(4)}
//   cmsghdr{len(0); level(4); type(8); data(12)}  -- 16-byte stride in the object code

// sendmsg(fd, msghdr*, flags) for a 64-bit guest. Gathers the scatter buffers
// into one scratch blob, translates any SCM_RIGHTS cmsg, builds a 32-bit msghdr
// in scratch, and calls the 32-bit ksendmsg (reusing all the AF_UNIX queue +
// fd-passing logic).
static U64 sys_sendmsg64(CPU64* cpu, U64 fd, U64 msg64, U64 flags) {
    KThread* thread = cpu->thread;
    if (!thread || !thread->process) return (U64)-K_ENOSYS;
    U32 base = msgScratch(thread);
    if (!base) return (U64)-K_EFAULT;
    KMemory* m32 = thread->memory;

    const U64 name64 = cpu->memory->readq(msg64);
    const U32 nameLength = name64 ? cpu->memory->readd(msg64 + 8) : 0;
    if (nameLength > 256) return (U64)-K_EINVAL;
    const U32 name32 = nameLength ? bounceSockaddrTo32(cpu, name64, nameLength, nullptr) : 0;
    if (nameLength && !name32) return (U64)-K_EFAULT;

    U64 iov     = cpu->memory->readq(msg64 + 16);
    U64 iovlen  = cpu->memory->readq(msg64 + 24);
    U64 control = cpu->memory->readq(msg64 + 32);
    U64 ctllen  = cpu->memory->readq(msg64 + 40);

    // Gather all iov segments into the scratch data buffer.
    U32 dataAddr = base + MSG_SCRATCH_DATA;
    U32 total = 0;
    U32 dataCap = MSG_SCRATCH_BYTES - MSG_SCRATCH_DATA;
    for (U64 i = 0; i < iovlen; i++) {
        U64 b = cpu->memory->readq(iov + 16 * i);
        U64 l = cpu->memory->readq(iov + 16 * i + 8);
        if (l == 0) continue;
        if (total + l > dataCap) l = dataCap - total;
        if (l == 0) break;
        std::vector<U8> tmp((size_t)l);
        cpu->memory->memcpyFromGuest(tmp.data(), b, (U32)l);
        m32->memcpy(dataAddr + total, tmp.data(), (U32)l);
        total += (U32)l;
    }

    // Build the 32-bit msghdr: a single iovec covering the gathered blob.
    m32->writed(base + MSG_SCRATCH_IOV + 0, dataAddr);
    m32->writed(base + MSG_SCRATCH_IOV + 4, total);
    m32->writed(base + MSG_SCRATCH_HDR + 0, name32);               // msg_name
    m32->writed(base + MSG_SCRATCH_HDR + 4, nameLength);           // msg_namelen
    m32->writed(base + MSG_SCRATCH_HDR + 8, base + MSG_SCRATCH_IOV); // msg_iov
    m32->writed(base + MSG_SCRATCH_HDR + 12, total ? 1 : 0);       // msg_iovlen
    U32 ctl32 = 0, ctllen32 = 0;
    if (control && ctllen >= 16) {
        // Translate SCM_RIGHTS: 64-bit cmsg {len(0),level(8),type(12),fds(16..)}.
        // The object's recv side reads 16-byte cmsg records with the fd at +12,
        // i.e. one fd per 16 bytes. Emit that compact form into the cmsg area.
        U64 clen  = cpu->memory->readq(control + 0);
        U32 level = cpu->memory->readd(control + 8);
        U32 type  = cpu->memory->readd(control + 12);
        if (level == K_SOL_SOCKET && type == K_SCM_RIGHTS) {
            U32 nfds = (U32)((clen - 16) / 4);
            U32 cm = base + MSG_SCRATCH_CMSG;
            for (U32 i = 0; i < nfds; i++) {
                U32 hostFd = cpu->memory->readd(control + 16 + 4 * i);
                m32->writed(cm + 16 * i + 0, 16);            // cmsg_len
                m32->writed(cm + 16 * i + 4, K_SOL_SOCKET);  // cmsg_level
                m32->writed(cm + 16 * i + 8, K_SCM_RIGHTS);  // cmsg_type
                m32->writed(cm + 16 * i + 12, hostFd);       // fd
            }
            ctl32 = cm;
            ctllen32 = nfds * 16;
        }
    }
    m32->writed(base + MSG_SCRATCH_HDR + 16, ctl32);              // msg_control
    m32->writed(base + MSG_SCRATCH_HDR + 20, ctllen32);          // msg_controllen
    m32->writed(base + MSG_SCRATCH_HDR + 24, 0);                 // msg_flags

    U32 rc = ksendmsg(thread, (U32)fd, base + MSG_SCRATCH_HDR, (U32)flags);
    // Record the SEND side of fd-passing in the crashring ('S'). get_handle_fd
    // (wineserver -> client) is the request that precedes the deterministic
    // teardown crash; seeing whether/which fd wineserver actually sent (and
    // whether ksendmsg succeeded) is the missing half — recvmsg's 'M'/'F' only
    // capture the receive side. e0 = #fds sent, e1 = the first guest fd value.
    if (wsReadEnabled() && ctllen32) {
        U32 firstFd = (control && ctllen >= 16) ? cpu->memory->readd(control + 16) : 0;
        crashRingRecordRead('S', (U32)thread->process->id, (U32)fd,
                            nullptr, 0, ctllen32 / 16, firstFd);
    }
    if (getenv("BW64_IPCDUMP")) {
        klog_fmt("IPC [pid=%d] sendmsg fd=%d datalen=%d ctllen32=%d -> rc=%d",
                 (int)thread->process->id, (int)fd, (int)total, (int)ctllen32, (int)(S32)rc);
    }
    return (U64)(S64)(S32)rc;
}

// recvmsg(fd, msghdr*, flags) for a 64-bit guest. Builds a 32-bit msghdr over a
// scratch data buffer + cmsg area, calls krecvmsg, then scatters the received
// bytes back into the 64-bit iov segments and translates any received
// SCM_RIGHTS fds into the 64-bit cmsg.
static U64 sys_recvmsg64(CPU64* cpu, U64 fd, U64 msg64, U64 flags) {
    KThread* thread = cpu->thread;
    if (!thread || !thread->process) return (U64)-K_ENOSYS;
    // BW64_WSCONC: wineserver reads protocol requests via recvmsg (SCM creds/fds),
    // so the concurrency witness must cover this path too — sys_read64's hook
    // alone reported 0 because the request stream never flows through read().
    // We bracket the whole call (krecvmsg blocks) so a second wineserver host
    // thread entering recvmsg while the first is still inside is visible.
    bool wsConcRm = wsConcEnabled() && thread->process->exe.contains("wineserver");
    struct WsConcGuard {
        bool on; CPU64* cpu;
        WsConcGuard(bool on, CPU64* cpu) : on(on), cpu(cpu) {
            if (on) {
                static bool announced = false;
                int now = ++g_wsConcInflight;
                if (!announced) { announced = true;
                    klog_fmt("WSCONC: wineserver recvmsg path armed (first hit) pid=%d tid=%d",
                             (int)cpu->thread->process->id, (int)cpu->thread->id); }
                if (now > 1)
                    klog_fmt("WSCONC: %d host threads concurrently in wineserver recvmsg! pid=%d tid=%d",
                             now, (int)cpu->thread->process->id, (int)cpu->thread->id);
            }
        }
        ~WsConcGuard() { if (on) --g_wsConcInflight; }
    } wsConcGuard(wsConcRm, cpu);
    U32 base = msgScratch(thread);
    if (!base) return (U64)-K_EFAULT;
    KMemory* m32 = thread->memory;

    const U64 name64 = cpu->memory->readq(msg64);
    const U32 nameLength = name64 ? cpu->memory->readd(msg64 + 8) : 0;
    const U32 name32 = name64 ? bounceSockaddrTo32(cpu, 0, std::min<U32>(nameLength, 256), nullptr) : 0;
    if (name64 && !name32) return (U64)-K_EFAULT;

    U64 iov     = cpu->memory->readq(msg64 + 16);
    U64 iovlen  = cpu->memory->readq(msg64 + 24);
    U64 control = cpu->memory->readq(msg64 + 32);
    U64 ctllen  = cpu->memory->readq(msg64 + 40);

    // Total receive capacity across all iov segments, capped to the scratch buf.
    U32 dataAddr = base + MSG_SCRATCH_DATA;
    U32 dataCap = MSG_SCRATCH_BYTES - MSG_SCRATCH_DATA;
    U32 want = 0;
    for (U64 i = 0; i < iovlen; i++) {
        U64 l = cpu->memory->readq(iov + 16 * i + 8);
        want += (U32)std::min<U64>(l, dataCap - want);
    }
    if (want > dataCap) want = dataCap;

    U32 cmArea = base + MSG_SCRATCH_CMSG;
    U32 cmCap = MSG_SCRATCH_DATA - MSG_SCRATCH_CMSG; // room for cmsg records
    m32->writed(base + MSG_SCRATCH_IOV + 0, dataAddr);
    m32->writed(base + MSG_SCRATCH_IOV + 4, want);
    m32->writed(base + MSG_SCRATCH_HDR + 0, name32);
    m32->writed(base + MSG_SCRATCH_HDR + 4, std::min<U32>(nameLength, 256));
    m32->writed(base + MSG_SCRATCH_HDR + 8, base + MSG_SCRATCH_IOV);
    m32->writed(base + MSG_SCRATCH_HDR + 12, 1);
    m32->writed(base + MSG_SCRATCH_HDR + 16, (control && ctllen) ? cmArea : 0);
    m32->writed(base + MSG_SCRATCH_HDR + 20, (control && ctllen) ? cmCap : 0);
    m32->writed(base + MSG_SCRATCH_HDR + 24, 0);

    U32 rc = krecvmsg(thread, (U32)fd, base + MSG_SCRATCH_HDR, (U32)flags);
    if (getenv("BW64_IPCDUMP")) {
        klog_fmt("IPC [pid=%d] recvmsg fd=%d want=%d flags=0x%x -> rc=%d ctllen32=%d",
                 (int)thread->process->id, (int)fd, (int)want, (unsigned)flags,
                 (int)(S32)rc, (int)m32->readd(base + MSG_SCRATCH_HDR + 20));
    }
    if (wsReadEnabled() && (S32)rc >= 0) {
        // 'M' record: the wineserver request-reply / fd-pass channel (the select
        // + get_apc_result loop the crash trace runs). hdr = first 16 bytes of
        // the received data; e0 = want, e1 = ctllen32 (bytes of SCM_RIGHTS).
        U8 head[16] = {0};
        U32 n = (rc < 16) ? (U32)rc : 16;
        if (n) m32->memcpy(head, dataAddr, n);
        crashRingRecordRead('M', (U32)thread->process->id, (U32)fd, head, (U32)rc,
                            want, (U32)m32->readd(base + MSG_SCRATCH_HDR + 20));
    }
    if ((S32)rc < 0) return (U64)(S64)(S32)rc;

    const U32 outNameLength = m32->readd(base + MSG_SCRATCH_HDR + 4);
    if (name64) {
        const U32 count = std::min<U32>(std::min<U32>(nameLength, outNameLength), 256);
        U8 address[256];
        if (count) {
            m32->memcpy(address, name32, count);
            cpu->memory->memcpyToGuest(name64, address, count);
        }
    }
    cpu->memory->writed(msg64 + 8, outNameLength);
    cpu->memory->writed(msg64 + 48, m32->readd(base + MSG_SCRATCH_HDR + 24));

    // Scatter the received bytes back into the 64-bit iov segments.
    // MSG_TRUNC may return the full datagram length, exceeding bytes buffered.
    U32 remaining = std::min<U32>(rc, want);
    U32 off = 0;
    for (U64 i = 0; i < iovlen && remaining; i++) {
        U64 b = cpu->memory->readq(iov + 16 * i);
        U64 l = cpu->memory->readq(iov + 16 * i + 8);
        U32 chunk = (U32)((l < remaining) ? l : remaining);
        if (chunk) {
            std::vector<U8> tmp(chunk);
            m32->memcpy(tmp.data(), dataAddr + off, chunk);
            cpu->memory->memcpyToGuest(b, tmp.data(), chunk);
            off += chunk;
            remaining -= chunk;
        }
    }

    // Translate any received SCM_RIGHTS fds (32-bit object wrote 16-byte cmsg
    // records, fd at +12) into the 64-bit cmsg the guest provided.
    U32 ctllen32 = m32->readd(base + MSG_SCRATCH_HDR + 20);
    if (control && ctllen >= 16 && ctllen32 >= 16) {
        U32 nfds = ctllen32 / 16;
        // NEVER write past the guest's control buffer (ctllen): a 64-bit cmsg
        // for nfds is 16 (cmsghdr) + nfds*4 bytes. Cap nfds so it fits — an
        // overflow here scribbles past wine/wineserver's heap-allocated control
        // buffer and corrupts its malloc arena ("unsorted double linked list").
        U32 maxFds = (ctllen >= 16) ? (U32)((ctllen - 16) / 4) : 0;
        if (nfds > maxFds) nfds = maxFds;
        U64 cmsgLen = 16 + (U64)nfds * 4; // 64-bit cmsghdr header is 16 bytes
        cpu->memory->writeq(control + 0, cmsgLen);
        cpu->memory->writed(control + 8, K_SOL_SOCKET);
        cpu->memory->writed(control + 12, K_SCM_RIGHTS);
        for (U32 i = 0; i < nfds; i++) {
            U32 fdv = m32->readd(cmArea + 16 * i + 12);
            cpu->memory->writed(control + 16 + 4 * i, fdv);
            if (wsReadEnabled()) {
                // 'F' witness: which fd was just handed to the guest via
                // SCM_RIGHTS. The same delivered fd value reappearing (e1) flags
                // a double-delivered wineserver object handle.
                crashRingRecordRead('F', (U32)thread->process->id, (U32)fd, nullptr, 0, nfds, fdv);
            }
        }
        cpu->memory->writeq(msg64 + 40, cmsgLen); // msg_controllen
    } else {
        cpu->memory->writeq(msg64 + 40, 0);
    }
    return (U64)(S64)(S32)rc;
}

// mincore(addr, length, vec): report which mapped pages have backing storage.
// Validate the complete queried range and output vector before writing any
// bytes, so an invalid range does not leave a partially-filled vector.
static U64 sys_mincore64(CPU64* cpu, U64 addr, U64 length, U64 vec) {
    if (!cpu || !cpu->memory || length == 0 || (addr & K64_PAGE_MASK))
        return (U64)-K_EINVAL;
    if (addr > ~0ULL - (length - 1)) return (U64)-K_ENOMEM;

    const U64 pageCount = ((length - 1) >> 12) + 1;
    const U64 firstPage = addr >> 12;
    const U64 vecEnd = vec + pageCount;
    if (vecEnd < vec) return (U64)-K_EFAULT;

    for (U64 i = 0; i < pageCount; ++i) {
        if (!cpu->memory->isPageMapped(firstPage + i)) return (U64)-K_ENOMEM;
        const U64 outAddr = vec + i;
        if (!cpu->memory->isPageMapped(outAddr >> 12)) return (U64)-K_EFAULT;
    }
    for (U64 i = 0; i < pageCount; ++i) {
        cpu->memory->writeb(vec + i,
            cpu->memory->getCommittedPagePtr(firstPage + i) ? 1 : 0);
    }
    return 0;
}

// Linux v6.12 arch/x86/entry/syscalls/syscall_64.tbl (common + 64 ABI).
// Keep the ABI names here complete through syscall 450 so every unknown
// dispatch logs the meaningful Linux name, including newly encountered gaps.
static const char* x64SyscallName(U64 nr) {
#ifdef BOXEDWINE_OPENGL
    if (nr == GL64_SYSCALL_NR) return "gl64_trap";
#endif
    switch (nr) {
        case 0: return "read";
        case 1: return "write";
        case 2: return "open";
        case 3: return "close";
        case 4: return "stat";
        case 5: return "fstat";
        case 6: return "lstat";
        case 7: return "poll";
        case 8: return "lseek";
        case 9: return "mmap";
        case 10: return "mprotect";
        case 11: return "munmap";
        case 12: return "brk";
        case 13: return "rt_sigaction";
        case 14: return "rt_sigprocmask";
        case 15: return "rt_sigreturn";
        case 16: return "ioctl";
        case 17: return "pread64";
        case 18: return "pwrite64";
        case 19: return "readv";
        case 20: return "writev";
        case 21: return "access";
        case 22: return "pipe";
        case 23: return "select";
        case 24: return "sched_yield";
        case 25: return "mremap";
        case 26: return "msync";
        case 27: return "mincore";
        case 28: return "madvise";
        case 29: return "shmget";
        case 30: return "shmat";
        case 31: return "shmctl";
        case 32: return "dup";
        case 33: return "dup2";
        case 34: return "pause";
        case 35: return "nanosleep";
        case 36: return "getitimer";
        case 37: return "alarm";
        case 38: return "setitimer";
        case 39: return "getpid";
        case 40: return "sendfile";
        case 41: return "socket";
        case 42: return "connect";
        case 43: return "accept";
        case 44: return "sendto";
        case 45: return "recvfrom";
        case 46: return "sendmsg";
        case 47: return "recvmsg";
        case 48: return "shutdown";
        case 49: return "bind";
        case 50: return "listen";
        case 51: return "getsockname";
        case 52: return "getpeername";
        case 53: return "socketpair";
        case 54: return "setsockopt";
        case 55: return "getsockopt";
        case 56: return "clone";
        case 57: return "fork";
        case 58: return "vfork";
        case 59: return "execve";
        case 60: return "exit";
        case 61: return "wait4";
        case 62: return "kill";
        case 63: return "uname";
        case 64: return "semget";
        case 65: return "semop";
        case 66: return "semctl";
        case 67: return "shmdt";
        case 68: return "msgget";
        case 69: return "msgsnd";
        case 70: return "msgrcv";
        case 71: return "msgctl";
        case 72: return "fcntl";
        case 73: return "flock";
        case 74: return "fsync";
        case 75: return "fdatasync";
        case 76: return "truncate";
        case 77: return "ftruncate";
        case 78: return "getdents";
        case 79: return "getcwd";
        case 80: return "chdir";
        case 81: return "fchdir";
        case 82: return "rename";
        case 83: return "mkdir";
        case 84: return "rmdir";
        case 85: return "creat";
        case 86: return "link";
        case 87: return "unlink";
        case 88: return "symlink";
        case 89: return "readlink";
        case 90: return "chmod";
        case 91: return "fchmod";
        case 92: return "chown";
        case 93: return "fchown";
        case 94: return "lchown";
        case 95: return "umask";
        case 96: return "gettimeofday";
        case 97: return "getrlimit";
        case 98: return "getrusage";
        case 99: return "sysinfo";
        case 100: return "times";
        case 101: return "ptrace";
        case 102: return "getuid";
        case 103: return "syslog";
        case 104: return "getgid";
        case 105: return "setuid";
        case 106: return "setgid";
        case 107: return "geteuid";
        case 108: return "getegid";
        case 109: return "setpgid";
        case 110: return "getppid";
        case 111: return "getpgrp";
        case 112: return "setsid";
        case 113: return "setreuid";
        case 114: return "setregid";
        case 115: return "getgroups";
        case 116: return "setgroups";
        case 117: return "setresuid";
        case 118: return "getresuid";
        case 119: return "setresgid";
        case 120: return "getresgid";
        case 121: return "getpgid";
        case 122: return "setfsuid";
        case 123: return "setfsgid";
        case 124: return "getsid";
        case 125: return "capget";
        case 126: return "capset";
        case 127: return "rt_sigpending";
        case 128: return "rt_sigtimedwait";
        case 129: return "rt_sigqueueinfo";
        case 130: return "rt_sigsuspend";
        case 131: return "sigaltstack";
        case 132: return "utime";
        case 133: return "mknod";
        case 134: return "uselib";
        case 135: return "personality";
        case 136: return "ustat";
        case 137: return "statfs";
        case 138: return "fstatfs";
        case 139: return "sysfs";
        case 140: return "getpriority";
        case 141: return "setpriority";
        case 142: return "sched_setparam";
        case 143: return "sched_getparam";
        case 144: return "sched_setscheduler";
        case 145: return "sched_getscheduler";
        case 146: return "sched_get_priority_max";
        case 147: return "sched_get_priority_min";
        case 148: return "sched_rr_get_interval";
        case 149: return "mlock";
        case 150: return "munlock";
        case 151: return "mlockall";
        case 152: return "munlockall";
        case 153: return "vhangup";
        case 154: return "modify_ldt";
        case 155: return "pivot_root";
        case 156: return "_sysctl";
        case 157: return "prctl";
        case 158: return "arch_prctl";
        case 159: return "adjtimex";
        case 160: return "setrlimit";
        case 161: return "chroot";
        case 162: return "sync";
        case 163: return "acct";
        case 164: return "settimeofday";
        case 165: return "mount";
        case 166: return "umount2";
        case 167: return "swapon";
        case 168: return "swapoff";
        case 169: return "reboot";
        case 170: return "sethostname";
        case 171: return "setdomainname";
        case 172: return "iopl";
        case 173: return "ioperm";
        case 174: return "create_module";
        case 175: return "init_module";
        case 176: return "delete_module";
        case 177: return "get_kernel_syms";
        case 178: return "query_module";
        case 179: return "quotactl";
        case 180: return "nfsservctl";
        case 181: return "getpmsg";
        case 182: return "putpmsg";
        case 183: return "afs_syscall";
        case 184: return "tuxcall";
        case 185: return "security";
        case 186: return "gettid";
        case 187: return "readahead";
        case 188: return "setxattr";
        case 189: return "lsetxattr";
        case 190: return "fsetxattr";
        case 191: return "getxattr";
        case 192: return "lgetxattr";
        case 193: return "fgetxattr";
        case 194: return "listxattr";
        case 195: return "llistxattr";
        case 196: return "flistxattr";
        case 197: return "removexattr";
        case 198: return "lremovexattr";
        case 199: return "fremovexattr";
        case 200: return "tkill";
        case 201: return "time";
        case 202: return "futex";
        case 203: return "sched_setaffinity";
        case 204: return "sched_getaffinity";
        case 205: return "set_thread_area";
        case 206: return "io_setup";
        case 207: return "io_destroy";
        case 208: return "io_getevents";
        case 209: return "io_submit";
        case 210: return "io_cancel";
        case 211: return "get_thread_area";
        case 212: return "lookup_dcookie";
        case 213: return "epoll_create";
        case 214: return "epoll_ctl_old";
        case 215: return "epoll_wait_old";
        case 216: return "remap_file_pages";
        case 217: return "getdents64";
        case 218: return "set_tid_address";
        case 219: return "restart_syscall";
        case 220: return "semtimedop";
        case 221: return "fadvise64";
        case 222: return "timer_create";
        case 223: return "timer_settime";
        case 224: return "timer_gettime";
        case 225: return "timer_getoverrun";
        case 226: return "timer_delete";
        case 227: return "clock_settime";
        case 228: return "clock_gettime";
        case 229: return "clock_getres";
        case 230: return "clock_nanosleep";
        case 231: return "exit_group";
        case 232: return "epoll_wait";
        case 233: return "epoll_ctl";
        case 234: return "tgkill";
        case 235: return "utimes";
        case 236: return "vserver";
        case 237: return "mbind";
        case 238: return "set_mempolicy";
        case 239: return "get_mempolicy";
        case 240: return "mq_open";
        case 241: return "mq_unlink";
        case 242: return "mq_timedsend";
        case 243: return "mq_timedreceive";
        case 244: return "mq_notify";
        case 245: return "mq_getsetattr";
        case 246: return "kexec_load";
        case 247: return "waitid";
        case 248: return "add_key";
        case 249: return "request_key";
        case 250: return "keyctl";
        case 251: return "ioprio_set";
        case 252: return "ioprio_get";
        case 253: return "inotify_init";
        case 254: return "inotify_add_watch";
        case 255: return "inotify_rm_watch";
        case 256: return "migrate_pages";
        case 257: return "openat";
        case 258: return "mkdirat";
        case 259: return "mknodat";
        case 260: return "fchownat";
        case 261: return "futimesat";
        case 262: return "newfstatat";
        case 263: return "unlinkat";
        case 264: return "renameat";
        case 265: return "linkat";
        case 266: return "symlinkat";
        case 267: return "readlinkat";
        case 268: return "fchmodat";
        case 269: return "faccessat";
        case 270: return "pselect6";
        case 271: return "ppoll";
        case 272: return "unshare";
        case 273: return "set_robust_list";
        case 274: return "get_robust_list";
        case 275: return "splice";
        case 276: return "tee";
        case 277: return "sync_file_range";
        case 278: return "vmsplice";
        case 279: return "move_pages";
        case 280: return "utimensat";
        case 281: return "epoll_pwait";
        case 282: return "signalfd";
        case 283: return "timerfd_create";
        case 284: return "eventfd";
        case 285: return "fallocate";
        case 286: return "timerfd_settime";
        case 287: return "timerfd_gettime";
        case 288: return "accept4";
        case 289: return "signalfd4";
        case 290: return "eventfd2";
        case 291: return "epoll_create1";
        case 292: return "dup3";
        case 293: return "pipe2";
        case 294: return "inotify_init1";
        case 295: return "preadv";
        case 296: return "pwritev";
        case 297: return "rt_tgsigqueueinfo";
        case 298: return "perf_event_open";
        case 299: return "recvmmsg";
        case 300: return "fanotify_init";
        case 301: return "fanotify_mark";
        case 302: return "prlimit64";
        case 303: return "name_to_handle_at";
        case 304: return "open_by_handle_at";
        case 305: return "clock_adjtime";
        case 306: return "syncfs";
        case 307: return "sendmmsg";
        case 308: return "setns";
        case 309: return "getcpu";
        case 310: return "process_vm_readv";
        case 311: return "process_vm_writev";
        case 312: return "kcmp";
        case 313: return "finit_module";
        case 314: return "sched_setattr";
        case 315: return "sched_getattr";
        case 316: return "renameat2";
        case 317: return "seccomp";
        case 318: return "getrandom";
        case 319: return "memfd_create";
        case 320: return "kexec_file_load";
        case 321: return "bpf";
        case 322: return "execveat";
        case 323: return "userfaultfd";
        case 324: return "membarrier";
        case 325: return "mlock2";
        case 326: return "copy_file_range";
        case 327: return "preadv2";
        case 328: return "pwritev2";
        case 329: return "pkey_mprotect";
        case 330: return "pkey_alloc";
        case 331: return "pkey_free";
        case 332: return "statx";
        case 333: return "io_pgetevents";
        case 334: return "rseq";
        case 335: return "uretprobe";
        case 424: return "pidfd_send_signal";
        case 425: return "io_uring_setup";
        case 426: return "io_uring_enter";
        case 427: return "io_uring_register";
        case 428: return "open_tree";
        case 429: return "move_mount";
        case 430: return "fsopen";
        case 431: return "fsconfig";
        case 432: return "fsmount";
        case 433: return "fspick";
        case 434: return "pidfd_open";
        case 435: return "clone3";
        case 436: return "close_range";
        case 437: return "openat2";
        case 438: return "pidfd_getfd";
        case 439: return "faccessat2";
        case 440: return "process_madvise";
        case 441: return "epoll_pwait2";
        case 442: return "mount_setattr";
        case 443: return "quotactl_fd";
        case 444: return "landlock_create_ruleset";
        case 445: return "landlock_add_rule";
        case 446: return "landlock_restrict_self";
        case 447: return "memfd_secret";
        case 448: return "process_mrelease";
        case 449: return "futex_waitv";
        case 450: return "set_mempolicy_home_node";
        default: return "unknown";
    }
}

// Every x86-64 syscall number without an implementation flows through here.
// Report once per ABI number (rather than once per process) so unsupported
// features remain visible without flooding logs in multi-threaded guests.
static U64 unimplementedSyscall64(U64 nr) {
    static std::mutex logMutex;
    static std::unordered_set<U32> reportedNumbers;
    bool firstReport;
    {
        std::lock_guard<std::mutex> lock(logMutex);
        firstReport = reportedNumbers.insert((U32)nr).second;
    }
    if (firstReport) {
        klog_fmt("ksyscall64: unimplemented syscall #%llu (%s)",
                 (unsigned long long)nr, x64SyscallName(nr));
    }
    return (U64)-K_ENOSYS;
}

#ifdef __EMSCRIPTEN__
static std::atomic<U32> bwThreadDumpMaxTid{0};
static inline bool bwThreadDumpEnabled() {
    static const bool enabled = []() {
        const char* value = getenv("BW_THREAD_DUMP");
        return value && strcmp(value, "1") == 0;
    }();
    return enabled;
}

// Best-effort guest stack walk for BW_THREAD_DUMP. Scans the qwords above the
// guest RSP for values that fall inside a PE image in the PEB loader list
// (candidate return addresses) and prints module+RVA. It is a heuristic scan
// (no unwind info), but it names the Windows module/function a thread is in
// even when the host-visible rip is just the libc syscall stub. Every read is
// guarded by isPageMapped so a racing/stale RSP cannot fault the dumper.
static void bwDumpGuestStack(CPU64* c, U32 pid, U32 tid) {
    KMemory64* mem = c->memory;
    if (!mem || !c->gsbase) return;
    auto readable = [&](U64 addr, U64 len) {
        for (U64 page = addr >> 12; page <= (addr + len - 1) >> 12; ++page)
            if (!mem->isPageMapped(page)) return false;
        return true;
    };
    if (!readable(c->gsbase + 0x60, 8)) return;
    U64 peb = mem->readq(c->gsbase + 0x60);
    if (!peb || !readable(peb + 0x18, 8)) return;
    U64 ldr = mem->readq(peb + 0x18);
    if (!ldr || !readable(ldr + 0x10, 8)) return;
    struct Mod { U64 base; U64 size; char name[40]; };
    Mod mods[96]; int nMods = 0;
    const U64 head = ldr + 0x10;
    U64 link = mem->readq(head);
    for (int i = 0; i < 128 && link && link != head && nMods < 96; ++i) {
        if (!readable(link, 0x68)) break;
        Mod& m = mods[nMods];
        m.base = mem->readq(link + 0x30);
        m.size = mem->readd(link + 0x40);
        m.name[0] = 0;
        U16 nlen = mem->readw(link + 0x58);
        U64 nbuf = mem->readq(link + 0x60);
        if (m.base && nlen && nlen < 80 && nbuf && readable(nbuf, nlen)) {
            int n = nlen / 2; if (n > 38) n = 38;
            for (int k = 0; k < n; ++k) { U16 ch = mem->readw(nbuf + (U64)k * 2); m.name[k] = ch < 128 ? (char)ch : '?'; }
            m.name[n] = 0;
        }
        if (m.base) ++nMods;
        link = mem->readq(link);
    }
    // Name the code the thread is executing when rip is outside every loader-list
    // module (anonymous/unlisted mapping): report the page flags and the nearest
    // preceding MZ/ELF header so a JIT'd or manually mapped image can be spotted.
    {
        const U64 rip = c->rip;
        bool inMod = false;
        for (int m = 0; m < nMods; ++m)
            if (rip >= mods[m].base && rip - mods[m].base < mods[m].size) {
                klog_fmt("BW_THREAD_DUMP rip pid=%u tid=%u %s+0x%llx", pid, tid,
                         mods[m].name[0] ? mods[m].name : "?", (unsigned long long)(rip - mods[m].base));
                inMod = true;
                break;
            }
        if (!inMod && (rip >> 20) != 0x700) { // 0x700... is the emulator's own syscall stub page
            const U64 page = rip >> 12;
            const bool mapped = mem->isPageMapped(page);
            U64 hdr = 0; const char* kind = "none";
            for (U64 back = 0; back < 4096 && mapped; ++back) {
                U64 a = (page - back) << 12;
                if (!readable(a, 4)) break;
                U32 w = mem->readd(a);
                if ((w & 0xffff) == 0x5a4d) { hdr = a; kind = "MZ"; break; }
                if (w == 0x464c457f) { hdr = a; kind = "ELF"; break; }
            }
            klog_fmt("BW_THREAD_DUMP rip pid=%u tid=%u unlisted rip=0x%llx mapped=%d flags=0x%x nearestHdr=%s@0x%llx",
                     pid, tid, (unsigned long long)rip, mapped ? 1 : 0,
                     mapped ? mem->getPageFlags(page) : 0, kind, (unsigned long long)hdr);
        }
    }
    // A thread inside a Wine syscall runs on the TEB kernel stack, so RSP is not on
    // the Windows stack. Wine keeps the saved PE register state in a struct
    // syscall_frame whose address lives at TEB+0x328 (see __wine_syscall_dispatcher:
    // rax 0x00, rdx 0x18, rsi 0x20, rdi 0x28, r8 0x30, r9 0x38, r10 0x40, rip 0x70,
    // rsp 0x88). Walk the Windows stack from the saved rsp and report the saved rip.
    const U64 stackBase = readable(c->gsbase + 8, 8) ? mem->readq(c->gsbase + 8) : 0;
    const U64 stackLimit = readable(c->gsbase + 0x10, 8) ? mem->readq(c->gsbase + 0x10) : 0;
    const U64 rsp = c->reg[X64_RSP].u64;
    const bool haveTib = stackBase > stackLimit && stackBase - stackLimit < (1ull << 32);
    U64 winRsp = rsp;
    if (!(haveTib && rsp >= stackLimit && rsp < stackBase) && readable(c->gsbase + 0x328, 8)) {
        const U64 frame = mem->readq(c->gsbase + 0x328);
        if (frame && readable(frame, 0xa0)) {
            winRsp = mem->readq(frame + 0x88);
            const U64 frip = mem->readq(frame + 0x70);
            const char* fmod = "?"; U64 fo = 0;
            for (int m = 0; m < nMods; ++m)
                if (frip >= mods[m].base && frip - mods[m].base < mods[m].size) { fmod = mods[m].name; fo = frip - mods[m].base; break; }
            klog_fmt("BW_THREAD_DUMP syscallframe pid=%u tid=%u rip=0x%llx %s+0x%llx rax=0x%llx r10=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx rsi=0x%llx rdi=0x%llx rsp=0x%llx",
                     pid, tid, (unsigned long long)frip, fmod, (unsigned long long)fo,
                     (unsigned long long)mem->readq(frame + 0x00), (unsigned long long)mem->readq(frame + 0x40),
                     (unsigned long long)mem->readq(frame + 0x18), (unsigned long long)mem->readq(frame + 0x30),
                     (unsigned long long)mem->readq(frame + 0x38), (unsigned long long)mem->readq(frame + 0x20),
                     (unsigned long long)mem->readq(frame + 0x28), (unsigned long long)winRsp);
            // Wait calls take a handle array through r10/rdx: show the first handles.
            const U64 hp = mem->readq(frame + 0x18);
            if (hp && readable(hp, 32))
                klog_fmt("BW_THREAD_DUMP syscallmem pid=%u tid=%u [rdx]=%08x %08x %08x %08x %08x %08x %08x %08x", pid, tid,
                         mem->readd(hp), mem->readd(hp + 4), mem->readd(hp + 8), mem->readd(hp + 12),
                         mem->readd(hp + 16), mem->readd(hp + 20), mem->readd(hp + 24), mem->readd(hp + 28));
        }
    }
    // A candidate return address must follow a call instruction.
    auto afterCall = [&](U64 v) {
        if (!readable(v - 8, 8)) return false;
        if (mem->readb(v - 5) == 0xE8) return true;
        if (mem->readb(v - 6) == 0xFF && (mem->readb(v - 5) & 0xF8) == 0x90) return true;
        if (mem->readb(v - 6) == 0xFF && mem->readb(v - 5) == 0x15) return true;
        if (mem->readb(v - 2) == 0xFF && (mem->readb(v - 1) & 0xF8) == 0xD0) return true;
        if (mem->readb(v - 3) == 0xFF && (mem->readb(v - 2) & 0xF8) == 0x50) return true;
        if (mem->readb(v - 3) == 0x41 && mem->readb(v - 2) == 0xFF && (mem->readb(v - 1) & 0xF8) == 0xD0) return true;
        if (mem->readb(v - 7) == 0xFF && mem->readb(v - 6) == 0x14) return true;
        return false;
    };
    klog_fmt("BW_THREAD_DUMP winstack pid=%u tid=%u rsp=0x%llx winRsp=0x%llx tib=[0x%llx,0x%llx)", pid, tid,
             (unsigned long long)rsp, (unsigned long long)winRsp,
             (unsigned long long)stackLimit, (unsigned long long)stackBase);
    int hits = 0;
    for (U64 i = 0; i < 6144 && hits < 64; ++i) {
        U64 at = winRsp + i * 8;
        if (!readable(at, 8)) break;
        if (haveTib && at >= stackBase) break;
        U64 v = mem->readq(at);
        for (int m = 0; m < nMods; ++m) {
            if (v >= mods[m].base && v - mods[m].base < mods[m].size) {
                if (afterCall(v)) {
                    klog_fmt("BW_THREAD_DUMP stack pid=%u tid=%u +0x%llx %s+0x%llx", pid, tid,
                             (unsigned long long)(i * 8), mods[m].name[0] ? mods[m].name : "?",
                             (unsigned long long)(v - mods[m].base));
                    ++hits;
                }
                break;
            }
        }
    }
}

// Called by the demo diagnostic timer only when the runtime URL opts in with
// ?bw64diag=threads. The wrapper maps that URL flag to BW_THREAD_DUMP before
// Emscripten caches ENV, so normal launches do not emit these snapshots.
extern "C" EMSCRIPTEN_KEEPALIVE void bw64_dump_guest_threads() {
    if (!bwThreadDumpEnabled()) return;

    U32 maxTid = bwThreadDumpMaxTid.load(std::memory_order_relaxed);
    klog_fmt("BW_THREAD_DUMP begin maxTid=%u", maxTid);
    for (U32 tid = 1; tid <= maxTid; ++tid) {
        KThread* thread = KSystem::getThreadById(tid);
        if (!thread || !thread->process) continue;
        const U32 pid = thread->process->id;
        const bool blocked = thread->waitingCond != nullptr;
        const char* state = blocked ? "blocked" : "running";
        const char* waitName = blocked ? thread->waitingCond->name.c_str() : "-";
#ifdef BOXEDWINE_GUEST_X64
        if (thread->cpu64) {
            CPU64* c = thread->cpu64;
            const U64 nr = c->reg[X64_RAX].u64;
            const U64 rip = blocked ? c->syscallRip : c->rip;
            klog_fmt("BW_THREAD_DUMP pid=%u tid=%u state=%s syscall=%s(#%llu) args=0x%llx,0x%llx,0x%llx,0x%llx,0x%llx,0x%llx rip=0x%llx wait=%s",
                     pid, thread->id, state,
                     blocked ? x64SyscallName(nr) : "-",
                     (unsigned long long)(blocked ? nr : 0),
                     (unsigned long long)c->reg[X64_RDI].u64,
                     (unsigned long long)c->reg[X64_RSI].u64,
                     (unsigned long long)c->reg[X64_RDX].u64,
                     (unsigned long long)c->reg[X64_R10].u64,
                     (unsigned long long)c->reg[X64_R8].u64,
                     (unsigned long long)c->reg[X64_R9].u64,
                     (unsigned long long)rip, waitName);
            if (blocked && thread->process &&
                (nr == X64_SYS_read || nr == X64_SYS_readv || nr == X64_SYS_recvfrom || nr == X64_SYS_recvmsg)) {
                KFileDescriptorPtr fd = thread->process->getFileDescriptor((FD)c->reg[X64_RDI].u64);
                std::shared_ptr<KUnixSocketObject> socket = fd && fd->kobject
                    ? std::dynamic_pointer_cast<KUnixSocketObject>(fd->kobject) : nullptr;
                if (socket) socket->debugDumpReadWait(pid, thread->id, (FD)c->reg[X64_RDI].u64);
            }
            if (blocked && thread->process && nr == X64_SYS_epoll_wait) {
                KFileDescriptorPtr fd = thread->process->getFileDescriptor((FD)c->reg[X64_RDI].u64);
                std::shared_ptr<KEPoll> epoll = fd && fd->kobject
                    ? std::dynamic_pointer_cast<KEPoll>(fd->kobject) : nullptr;
                if (epoll) epoll->debugDumpRegistrations(thread->process.get(), pid, thread->id);
            }
            bwDumpGuestStack(c, pid, thread->id);
            continue;
        }
#endif
        CPU* c = thread->cpu;
        const U32 nr = c ? c->reg[0].u32 : 0;
        klog_fmt("BW_THREAD_DUMP pid=%u tid=%u state=%s syscall=%s(#%u) args=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x rip=0x%x wait=%s",
                 pid, thread->id, state, blocked ? "x86" : "-", blocked ? nr : 0,
                 c ? c->reg[3].u32 : 0, c ? c->reg[1].u32 : 0,
                 c ? c->reg[2].u32 : 0, c ? c->reg[6].u32 : 0,
                 c ? c->reg[7].u32 : 0, c ? c->reg[5].u32 : 0,
                 c ? c->eip.u32 : 0, waitName);
    }
    if (getenv("BW64_CRASHRING")) crashRingDump("thread-dump");
    klog_fmt("BW_THREAD_DUMP end");
}
#endif

void ksyscall64(CPU64* cpu) {
    if (!cpu) return;
#ifdef __EMSCRIPTEN__
    if (cpu->thread) {
        U32 tid = cpu->thread->id;
        U32 seen = bwThreadDumpMaxTid.load(std::memory_order_relaxed);
        while (seen < tid && !bwThreadDumpMaxTid.compare_exchange_weak(
                   seen, tid, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }
#endif
    U64 nr   = cpu->reg[X64_RAX].u64;
    U64 a1   = cpu->reg[X64_RDI].u64;
    U64 a2   = cpu->reg[X64_RSI].u64;
    U64 a3   = cpu->reg[X64_RDX].u64;
    U64 a4   = cpu->reg[X64_R10].u64;
    U64 a5   = cpu->reg[X64_R8].u64;
    U64 a6   = cpu->reg[X64_R9].u64;
    U64 ret  = (U64)-K_ENOSYS;

    // Live metrics: the syscall / host-bridge span is measured around the whole
    // dispatch so the panel can separate interpreter traffic from the GL and
    // Vulkan traps, which time themselves (see bw64metrics.h). The GL/VK traps
    // are excluded here so they are never double-counted.
    const bool bw64MetricsOn = bw64_metrics_enabled() != 0;
    bool bw64BridgeTrap = false;
#ifdef BOXEDWINE_OPENGL
    bw64BridgeTrap = bw64BridgeTrap || nr == GL64_SYSCALL_NR;
#endif
#ifdef BOXEDWINE_VULKAN64
    bw64BridgeTrap = bw64BridgeTrap || nr == VK64_SYSCALL_NR;
#endif
    const double bw64T0 = bw64MetricsOn ? bw64_metrics_now_ms() : 0.0;

    // Set BW64_SYSTRACE=1 to log every 64-bit syscall — invaluable for wine
    // bring-up (finding where a guest stalls or which syscall is next missing).
    // BW64_TRACEPID=N traces ONLY process N (one cheap compare; doesn't slow the
    // rest of the system, so it can catch a timing-sensitive failure that full
    // SYSTRACE masks). Useful to see exactly which syscall precedes a process's
    // exit_group(1).
    {
        static const char* tracePidEnv = getenv("BW64_TRACEPID");
        static int tracePid = tracePidEnv ? atoi(tracePidEnv) : -1;
        // BW64_TRACE_EXE=<substr> traces every process whose commandLine contains
        // <substr> — pid-independent, so it survives wine's loader pid churn (the
        // wine64 wrapper exits under one pid while the app runs under another).
        // Used to find which Win32 startup syscall precedes a GUI app's silent exit.
        static const char* traceExe = getenv("BW64_TRACE_EXE");
        int myPid = (int)(cpu->thread ? cpu->thread->process->id : -1);
        bool exeMatch = false;
        if (traceExe && cpu->thread && cpu->thread->process &&
            cpu->thread->process->commandLine.contains(traceExe)) {
            exeMatch = true;
        }
        if (getenv("BW64_SYSTRACE") || (tracePid >= 0 && myPid == tracePid) || exeMatch) {
            klog_fmt("SYS64 [pid=%d] #%llu %s (a1=0x%llx a2=0x%llx a3=0x%llx)",
                     myPid, (unsigned long long)nr, x64SyscallName(nr),
                     (unsigned long long)a1, (unsigned long long)a2,
                     (unsigned long long)a3);
        }
    }

    // BW64_MEMSTATS=1 — periodically log the guest-memory footprint + host RSS so
    // we can SEE the leak (mapped pages climb monotonically while munmap is a
    // no-op) and the boot slowdown (per-syscall cost rises with the page map).
    // Throttled to once per ~2s wall so it never perturbs the timing-sensitive
    // race. Env read once (static cache); near-zero cost when off.
    {
        static const bool memStats = getenv("BW64_MEMSTATS") != nullptr;
        if (memStats && cpu->memory && cpu->thread && cpu->thread->process) {
            // Per-pid syscall accounting for the livelock (Mode 2) diagnosis: a
            // busy-poll storm shows one pid spinning the SAME syscall thousands of
            // times between ticks while no real progress is logged. pids stay
            // small during boot; cap the table and fold the rest into slot 0.
            // Exact per-PID syscall histogram for opt-in interval reports.
            // Linux x86-64 syscall IDs occupy 0..511 here; retain the private
            // GL/Vulkan trap IDs separately and fold any other high ID to 512.
            static std::atomic<U32> syscallCounts[256][515];
            int mypid = (int)cpu->thread->process->id;
            U32 slot = (mypid >= 0 && mypid < 256) ? (U32)mypid : 0;
            U32 syscallBucket = nr < 512 ? (U32)nr : 512;
#ifdef BOXEDWINE_OPENGL
            if (nr == GL64_SYSCALL_NR) syscallBucket = 513;
#endif
#ifdef BOXEDWINE_VULKAN64
            if (nr == VK64_SYSCALL_NR) syscallBucket = 514;
#endif
            syscallCounts[slot][syscallBucket].fetch_add(1, std::memory_order_relaxed);

            static const U32 memStatsIntervalMs = []() {
                const char* value = getenv("BW64_MEMSTATS_INTERVAL_MS");
                if (!value || !value[0]) return 2000U;
                char* end = nullptr;
                unsigned long parsed = strtoul(value, &end, 10);
                return end != value && *end == '\0' && parsed >= 1000 && parsed <= 300000
                    ? (U32)parsed : 2000U;
            }();

            static std::atomic<U64> lastUs{0};
            U64 nowUs = KSystem::getSystemTimeAsMicroSeconds();
            U64 prev = lastUs.load(std::memory_order_relaxed);
            if (prev == 0) {
                lastUs.compare_exchange_strong(prev, nowUs, std::memory_order_relaxed);
            } else if (nowUs - prev >= (U64)memStatsIntervalMs * 1000ULL &&
                lastUs.compare_exchange_strong(prev, nowUs, std::memory_order_relaxed)) {
                KMemory64* mem = cpu->memory;
                U64 mapped = mem->mappedPageCount();
                U64 committed = mem->committedPageCount();
                U64 rss = KSystem::getHostResidentBytes();
                // Find the busiest pid this interval (the livelock suspect) and
                // reset all counters for the next window.
                U32 topPid = 0, topCount = 0;
                auto syscallNameForBucket = [](U32 syscallNr) {
                    if (syscallNr == 512) return "other";
                    if (syscallNr == 513) return "gl64_trap";
                    if (syscallNr == 514) return "vk64_trap";
                    return x64SyscallName(syscallNr);
                };
                for (U32 i = 0; i < 256; i++) {
                    U32 topNr[3] = {}, topCalls[3] = {};
                    U32 pidCalls = 0;
                    for (U32 syscallNr = 0; syscallNr < 515; syscallNr++) {
                        U32 calls = syscallCounts[i][syscallNr].exchange(0, std::memory_order_relaxed);
                        pidCalls += calls;
                        if (calls > topCalls[0]) {
                            topCalls[2] = topCalls[1]; topNr[2] = topNr[1];
                            topCalls[1] = topCalls[0]; topNr[1] = topNr[0];
                            topCalls[0] = calls; topNr[0] = syscallNr;
                        } else if (calls > topCalls[1]) {
                            topCalls[2] = topCalls[1]; topNr[2] = topNr[1];
                            topCalls[1] = calls; topNr[1] = syscallNr;
                        } else if (calls > topCalls[2]) {
                            topCalls[2] = calls; topNr[2] = syscallNr;
                        }
                    }
                    if (pidCalls > topCount) { topCount = pidCalls; topPid = i; }
                    if (pidCalls) {
                        const char* name0 = syscallNameForBucket(topNr[0]);
                        const char* name1 = topCalls[1] ? syscallNameForBucket(topNr[1]) : "-";
                        const char* name2 = topCalls[2] ? syscallNameForBucket(topNr[2]) : "-";
                        klog_fmt("MEMSTATS_SYSCALLS pid=%u calls=%u intervalMs=%u top1=%s(#%u):%u top2=%s(#%u):%u top3=%s(#%u):%u",
                                 i, pidCalls, memStatsIntervalMs,
                                 name0, topNr[0], topCalls[0],
                                 name1, topNr[1], topCalls[1],
                                 name2, topNr[2], topCalls[2]);
                    }
                }
                klog_fmt("MEMSTATS pid=%d pages=%llu committedKB=%llu rssKB=%llu "
                         "busiest=pid%u/%usc intervalMs=%u",
                         (int)cpu->thread->process->id,
                         (unsigned long long)mapped,
                         (unsigned long long)(committed * 4),
                         (unsigned long long)(rss / 1024),
                         topPid, topCount, memStatsIntervalMs);
            }
        }
    }

    switch (nr) {
#ifdef BOXEDWINE_OPENGL
        case GL64_SYSCALL_NR:
            // Private guest->host OpenGL trap (see source/opengl/gl64bridge*).
            // RDI=fn id, RSI=guest VA of GL64Args. Result goes back in RAX.
            ret = gl64Bridge(cpu, a1, a2);
            break;
#endif
#ifdef BOXEDWINE_VULKAN64
        case VK64_SYSCALL_NR:
            // Private guest->host Vulkan trap (see source/vulkan/vk64bridge*).
            // RDI=fn id, RSI=guest VA of VK64Args. Result goes back in RAX.
            ret = vk64Bridge(cpu, a1, a2);
            break;
#endif
        case X64_SYS_write:
            ret = sys_write64(cpu, a1, a2, a3);
            break;
        case X64_SYS_writev:
            ret = sys_writev64(cpu, a1, a2, a3);
            break;
        case X64_SYS_prctl:
            // prctl(option, ...). We don't model process control state
            // (thread name, dumpable flag, seccomp, etc.). Return success for
            // the common set-style options glibc/busybox issue at startup
            // (PR_SET_NAME=15, PR_SET_DUMPABLE=4, PR_SET_PDEATHSIG=1,
            // PR_SET_VMA=0x53564d41) so callers don't treat it as fatal;
            // PR_CAPBSET_READ(23)/PR_GET_*(even) return 0. A blanket 0 is the
            // pragmatic choice for a single-process interpreter.
            ret = 0;
            break;
        case X64_SYS_arch_prctl:
            ret = sys_arch_prctl64(cpu, a1, a2);
            break;
        case X64_SYS_brk:
            ret = sys_brk64(cpu, a1);
            break;
        case X64_SYS_mmap:
            ret = sys_mmap64(cpu, a1, a2, a3, a4, a5, a6);
            break;
        case X64_SYS_mprotect:
            // Flags-only: record the new protection on the page flags (real impl
            // in KMemory64::mprotect). The interpreter does NOT enforce
            // protection (it reads/writes unconditionally, and RELRO/JIT rely on
            // that lenience), so this is pure bookkeeping — it lets munmap and the
            // gap search reason about wine's PROT_NONE reservations vs committed
            // anon pages. We do NOT free buffers on PROT_NONE: that is the same
            // lazy-re-read hazard that makes decommit-on-munmap unsafe.
            //
            // KMemory64::mprotect returns the addr (mmap-style) on success; the
            // mprotect SYSCALL must return 0. Translate: pass through a negative
            // errno, otherwise report success. (Returning addr made ld.so read it
            // as -errno -> "cannot apply additional memory protection after
            // relocation" -> exit 127.)
            {
                U64 r = cpu->memory->mprotect(a1, a2, a3);
                ret = ((S64)r < 0) ? r : 0;
            }
            break;
        case X64_SYS_munmap:
            // Address-space-only free: KMemory64::munmap trims the ranges
            // reservation record so the region is reusable and the gap search
            // stays fast, WITHOUT freeing page backing buffers. Freeing buffers
            // regresses boot (wine/wineserver lazily re-read munmap'd regions —
            // zero-filled reads trip asserts), so the backing store is kept; the
            // memory is already bounded by lazy commit on the mmap side.
            ret = cpu->memory->munmap(a1, a2);
            break;
        case X64_SYS_mincore:
            ret = sys_mincore64(cpu, a1, a2, a3);
            break;
        case X64_SYS_shmget:
            ret = sys_shmget64(cpu, a1, a2, a3);
            break;
        case X64_SYS_shmat:
            ret = sys_shmat64(cpu, a1, a2, a3);
            break;
        case X64_SYS_shmctl:
            ret = sys_shmctl64(cpu, a1, a2, a3);
            break;
        case X64_SYS_shmdt:
            ret = sys_shmdt64(cpu, a1);
            break;
        case X64_SYS_set_tid_address:
            // set_tid_address(tidptr): records the address the kernel must
            // zero + futex-wake when this thread exits (CLONE_CHILD_CLEARTID
            // semantics). glibc calls this for every thread; pthread_join
            // relies on the wake. Returns the caller's tid.
            if (cpu->thread) {
                cpu->thread->clear_child_tid64 = a1;
            }
            ret = cpu->thread ? cpu->thread->id : 1;
            break;
        case X64_SYS_rt_sigaction:
            ret = sys_rt_sigaction64(cpu, a1, a2, a3, a4);
            break;
        case X64_SYS_rt_sigprocmask:
            ret = sys_rt_sigprocmask64(cpu, a1, a2, a3, a4);
            break;
        case X64_SYS_sigaltstack:
            ret = sys_sigaltstack64(cpu, a1, a2);
            break;
        case X64_SYS_utimensat:
            // utimensat(dirfd, path, times[2], flags) — set file atime/mtime.
            // fontconfig touches its cache files' timestamps; returning -ENOSYS
            // made it retry (43+ calls seen). We don't model per-file mtime
            // precisely, so report success — the cache write itself already
            // persisted the data. (a1=dirfd, a2=path, a3=times, a4=flags.)
            ret = 0;
            break;
        case X64_SYS_set_robust_list:
        case X64_SYS_madvise:
        case X64_SYS_fadvise64:
            // ld-linux makes these calls before main; safe to no-op.
            // fadvise64(fd, offset, len, advice): a pure readahead hint
            // (fontconfig POSIX_FADV_WILLNEED/RANDOM on its cache mmaps).
            // The interpreter has no page cache to advise, so ignoring it is
            // correct — the data is still read on demand.
            ret = 0;
            break;
        case X64_SYS_ioctl: {
            // ioctl(fd, request, arg). The old blanket no-op (return 0) made
            // wine believe a device-type probe on a regular file SUCCEEDED, so
            // it spun forever re-issuing ioctl(0x82307201) on the PE exe it had
            // just opened. But routing blindly to kobject->ioctl is also wrong:
            // the tty/console ioctls (TCGETS, FIONREAD, VT_*, ...) write their
            // result struct back through thread->memory (the 32-bit KMemory),
            // so for a 64-bit guest they scribble the wrong address and fault.
            //
            // For a 64-bit guest the correct, safe answers here are:
            //  - FIONBIO (0x5421): set/clear non-blocking — route to the kobject
            //    via fcntl semantics; the arg is a single int we read ourselves.
            //  - FIONREAD (0x541B): bytes available — we don't track it; report 0.
            //  - everything else (TCGETS device probes, the 0x82307201 query,
            //    KDSKBMUTE, ...): -ENOTTY, which is what a real kernel returns
            //    for a regular file / non-tty and what wine's fallback expects.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            U32 cmd = (U32)a2;
            // Device ioctls (M5 sound bridge): give the fd's open node first
            // shot via the 64-bit-aware ioctl64 (default returns ENOTTY, so
            // only devices that opt in — /dev/dsp — change behavior here).
            {
                KFileDescriptorPtr fdesc = cpu->thread->process->getFileDescriptor((FD)(S32)a1);
                if (fdesc) {
                    std::shared_ptr<KFile> kf = std::dynamic_pointer_cast<KFile>(fdesc->kobject);
                    if (kf && kf->openFile) {
                        U32 r = kf->openFile->ioctl64(cmd, a3, cpu->memory);
                        if ((S32)r != -K_ENOTTY) { ret = (U64)(S64)(S32)r; break; }
                    }
                }
            }
            if (cmd == 0x5421) { // FIONBIO: arg = int* (0=blocking, !=0 nonblocking)
                U32 on = a3 ? cpu->memory->readd(a3) : 0;
                cpu->thread->process->fcntrl(cpu->thread, (FD)(S32)a1, K_F_SETFL,
                                             on ? K_O_NONBLOCK : 0);
                ret = 0;
            } else if (cmd == 0x541B) { // FIONREAD: report 0 bytes pending
                if (a3) cpu->memory->writed(a3, 0);
                ret = 0;
            } else {
                ret = (U64)-K_ENOTTY;
            }
            break;
        }
        case X64_SYS_chdir: {
            // chdir(path) — MUST be real: wine chdir's into its WINEPREFIX and
            // then stats "." to validate it. A no-op left cwd at the (missing)
            // default /home/username, so that "." stat failed with ENOENT and
            // wine reported "could not find the prefix". Route to KProcess::chdir.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            char path[1024] = {0};
            cpu->memory->memcpyFromGuest(path, a1, sizeof(path) - 1);
            ret = (U64)(S64)(S32)cpu->thread->process->chdir(BString::copy(path));
            break;
        }
        case X64_SYS_mremap:
            // mremap(old, oldlen, newlen, flags, newaddr). v1: return old
            // address — glibc malloc only resizes when MREMAP_MAYMOVE is set
            // and tolerates a noop if no growth happens. This is wrong but
            // surfaces obvious failures at the malloc level, not later.
            ret = a1;
            break;
        case X64_SYS_dup:
            if (cpu->thread && cpu->thread->process) {
                U32 newFd = cpu->thread->process->dup((U32)a1);
                ret = (S32)newFd < 0 ? (U64)(S64)(S32)newFd : (U64)newFd;
            } else {
                ret = (U64)-K_ENOSYS;
            }
            break;
        case X64_SYS_dup2:
            if (cpu->thread && cpu->thread->process) {
                U32 newFd = cpu->thread->process->dup2((FD)a1, (FD)a2);
                ret = (S32)newFd < 0 ? (U64)(S64)(S32)newFd : (U64)newFd;
            } else {
                ret = (U64)-K_ENOSYS;
            }
            break;
        case X64_SYS_getcwd: {
            // getcwd(buf, size) — copy current directory string out. Returns
            // the byte count including NUL on success (the libc wrapper
            // returns a pointer), -ERANGE if size is too small.
            if (!a1 || a2 == 0) { ret = (U64)-K_EFAULT; break; }
            // No-process standalone runner: default cwd to "/".
            BString cwd = (cpu->thread && cpu->thread->process)
                              ? cpu->thread->process->currentDirectory : B("/");
            if (!cwd.length()) cwd = B("/");
            U64 need = (U64)cwd.length() + 1;
            if (need > a2) { ret = (U64)-34; /* -ERANGE */ break; }
            cpu->memory->memcpyToGuest(a1, cwd.c_str(), need);
            ret = need;
            break;
        }
        case X64_SYS_fcntl: {
            // fcntl(fd, cmd, arg). Route to the width-agnostic KProcess::fcntrl,
            // which returns the *real* per-fd state — crucially F_GETFL must
            // report the fd's actual access mode (O_RDONLY/WRONLY/RDWR). The
            // old stub returned 0 (== O_RDONLY) for every get, which made
            // glibc's fdopen(fd, "w") fail with EINVAL when wineserver wrote
            // its registry temp file (O_WRONLY) — that was the "could not save
            // registry branch ... Invalid argument" abort.
            //
            // The lock commands (F_GETLK/F_SETLK/F_SETLKW) take a `struct flock*`
            // in `arg`; fcntrl reads/writes it through thread->memory (the 32-bit
            // KMemory), but for a 64-bit guest the struct lives in cpu->memory
            // (KMemory64). So for those we bounce the flock through the 32-bit
            // scratch and use the 64-bit cmd variant (readFileLock(is64=true)
            // layout: l_type@0 w, l_whence@2 w, l_start@4 q, l_len@12 q,
            // l_pid@20 d — 24 bytes). wineserver F_SETLKs its registry/lock
            // files; without this it page-faulted reading the lock arg from the
            // empty 32-bit memory.
            if (!cpu->thread || !cpu->thread->process || !cpu->thread->memory) {
                ret = (U64)-K_ENOSYS; break;
            }
            U32 cmd = (U32)a2;
            bool isLock = (cmd == K_F_GETLK || cmd == K_F_SETLK || cmd == K_F_SETLKW ||
                           cmd == K_F_GETLK64 || cmd == K_F_SETLK64 || cmd == K_F_SETLKW64);
            if (!isLock) {
                ret = (U64)(S64)(S32)cpu->thread->process->fcntrl(
                    cpu->thread, (FD)(S32)a1, cmd, (U32)a3);
                break;
            }
            // Lock command: marshal the 24-byte 64-bit flock into scratch.
            const U32 FLOCK64_BYTES = 24;
            U32 scratch = bounceSockaddrTo32(cpu, 0, 0, nullptr);
            if (!scratch) { ret = (U64)-K_EFAULT; break; }
            {
                U8 tmp[FLOCK64_BYTES];
                cpu->memory->memcpyFromGuest(tmp, a3, FLOCK64_BYTES);
                cpu->thread->memory->memcpy(scratch, tmp, FLOCK64_BYTES);
            }
            // Use the *64-bit* cmd variant so fcntrl's readFileLock reads the
            // 64-bit layout from the scratch we just populated.
            U32 cmd64 = (cmd == K_F_GETLK)  ? K_F_GETLK64
                      : (cmd == K_F_SETLK)  ? K_F_SETLK64
                      : (cmd == K_F_SETLKW) ? K_F_SETLKW64
                      : cmd;
            ret = (U64)(S64)(S32)cpu->thread->process->fcntrl(
                cpu->thread, (FD)(S32)a1, cmd64, scratch);
            // F_GETLK writes the resulting lock back into the struct.
            if (cmd == K_F_GETLK || cmd == K_F_GETLK64) {
                U8 tmp[FLOCK64_BYTES];
                cpu->thread->memory->memcpy(tmp, scratch, FLOCK64_BYTES);
                cpu->memory->memcpyToGuest(a3, tmp, FLOCK64_BYTES);
            }
            break;
        }
        case X64_SYS_tkill:   // tkill(tid, sig)         — a2 unused (tid in a1)
        case X64_SYS_tgkill: { // tgkill(tgid, tid, sig) — tid in a2
            // Deliver a signal to a specific thread, possibly in ANOTHER process.
            // wineserver does exactly this: send_thread_signal() (server/ptrace.c)
            // does tkill/tgkill(client_pid, client_tid, SIGUSR1) to make a client
            // thread run a queued APC. The old handler only ever signaled SELF
            // (returned ESRCH for any tid != our own), so wineserver's APC
            // delivery ALWAYS failed -> queue_apc() returned 0 -> async_terminate
            // fell through to a SYNCHRONOUS async_set_result that dropped the
            // async-queue's reference EARLY -> free_async_queue then released it
            // AGAIN -> the deterministic teardown double-free (bug #2: the
            // release_object refcount assert / tcache / double-linked-list faces).
            // Now: tgkill carries tid in a2, tkill in a1; route cross-thread
            // through a real per-thread delivery.
            bool isTgkill = (nr == X64_SYS_tgkill);
            U32 targetTid = (U32)(isTgkill ? a2 : a1);
            U32 sig       = (U32)(isTgkill ? a3 : a2);
            U64 ourTid    = cpu->thread ? (U64)cpu->thread->id : 1;
            if (targetTid != (U32)ourTid) {
                KThread* target = KSystem::getThreadById(targetTid);
                if (!target) { ret = (U64)-K_ESRCH; break; }
                if (sig == 0) { ret = 0; break; } // liveness probe — target exists
                // Don't arm a pending signal on a thread that is already tearing
                // down: it will never reach a normal scheduler slice to take it,
                // and a thread parked in futex64's wait loop during exit would
                // otherwise keep trying to deliver it mid-kill — a teardown
                // deadlock. The caller still sees success (the thread is on its
                // way out, which is what a signal would have accomplished).
                if (target->terminating) { ret = 0; break; }
                // Async cross-thread delivery: arm the target's pending-signal;
                // the 64-bit scheduler delivers it (deliverSignalSync) at the
                // target's next slice, before it runs guest code. We can't build
                // a signal frame here because the target may be executing on a
                // different host thread.
                {
                    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(target->pendingSignalsMutex);
                    target->pendingSignals |= (1ULL << (sig - 1));
                }
                // A target parked in a blocking syscall (wineserver socket read,
                // poll, futex, sleep) never reaches a scheduler slice or the
                // 64k-instruction check, so kick the condition it sleeps on;
                // its wait loop sees pendingSignals and calls
                // interruptSyscallWithSignal. Waits that are not on a condition
                // fall back to their short timed slices (see
                // BOXEDWINE_CONDITION_WAIT_SIGNALABLE).
#ifdef BOXEDWINE_MULTI_THREADED
                {
                    BOXEDWINE_CONDITION cond = target->waitingCond;
                    if (cond) {
                        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(cond);
                        BOXEDWINE_CONDITION_SIGNAL_ALL(cond);
                    }
                }
#endif
                ret = 0;
                break;
            }
            if (sig == 0) {
                // sig=0 to our own tid is a liveness probe — we're alive.
                ret = 0;
                break;
            }
            // Set RAX to the syscall's return value *before* building the
            // signal frame so the frame captures RAX=0 (success). Otherwise
            // rt_sigreturn restores RAX to whatever was in it at entry
            // (the syscall number 234), and userspace sees tgkill "fail".
            cpu->reg[X64_RAX].setU64(0);
            if (deliverSignalSync(cpu, sig)) {
                // Handler will run next; ret=0 (kernel reports success
                // *before* handler runs, and the syscall return value is
                // overwritten by RAX-restore at rt_sigreturn anyway).
                ret = 0;
            } else {
                // No handler installed / SIG_DFL / SIG_IGN. For most
                // signals this should terminate the process; for v1 we
                // log and return 0 so abort()-paths can be observed.
                klog_fmt("ksyscall64: tgkill self sig=%u — no handler installed", sig);
                ret = 0;
            }
            break;
        }
        case X64_SYS_rt_sigreturn: {
            // x86-64 ABI: at signal-delivery time the kernel set
            //   RSP = uctxPtr - 8     // points at pushed restorer_addr
            // The handler's terminating `ret` pops that slot, leaving
            //   RSP = uctxPtr         // which is what the restorer sees
            // before issuing `syscall rt_sigreturn`. The ucontext_t
            // therefore lives at the *current* RSP. We read all
            // gprs/rip/rflags/sigmask out of it and overwrite our state.
            // The saved RAX becomes the syscall return value (kernel
            // restores the pre-signal RAX, *not* a sigreturn status).
            if (!cpu->memory) { ret = (U64)-K_EFAULT; break; }
            U64 uctxPtr = cpu->reg[X64_RSP].u64;
            ret = restoreSignalFrame(cpu, uctxPtr);
            // restoreSignalFrame wrote rip/rsp/gprs (all except RAX) directly.
            // The dispatcher writes `ret` (savedRax) into RAX after the break,
            // completing the restore. The cpu->rip we just wrote is final —
            // SYSCALL only advances RIP *before* ksyscall64() is invoked, so
            // our mid-handler write to cpu->rip survives.
            break;
        }
        case X64_SYS_read:
            ret = sys_read64(cpu, a1, a2, a3);
            break;
        case X64_SYS_open:
        case X64_SYS_openat:
            // open(path, flags, mode) — same arg layout once we shift one.
            if (nr == X64_SYS_open) ret = sys_openat64(cpu, ~0ULL, a1, a2, a3);
            else                    ret = sys_openat64(cpu, a1,    a2, a3, a4);
            break;
        case X64_SYS_close:
            ret = sys_close64(cpu, a1);
            break;
        case X64_SYS_fstat:
            ret = sys_fstat64(cpu, a1, a2);
            break;
        case X64_SYS_stat:
            ret = sys_stat_path64(cpu, a1, a2, true);
            break;
        case X64_SYS_lstat:
            ret = sys_stat_path64(cpu, a1, a2, false);
            break;
        case X64_SYS_newfstatat:
            ret = sys_newfstatat64(cpu, a1, a2, a3, a4);
            break;
        case X64_SYS_statfs:
            // a1 = path (ignored — same lie for every fs), a2 = struct statfs*
            ret = sys_statfs64_common(cpu, a2);
            break;
        case X64_SYS_fstatfs:
            // a1 = fd (ignored), a2 = struct statfs*
            ret = sys_statfs64_common(cpu, a2);
            break;
        // Extended attributes: our guest FS has none. get*xattr -> -ENODATA
        // ("attribute does not exist"), the answer Linux gives for a file with
        // no xattrs — this is what wine's ntdll probes for and tolerates. The
        // set*/list*/remove* variants likewise report "unsupported/empty".
        // Without these, wine64 startup tripped the unimplemented-syscall path
        // (#191 getxattr) during prefix/service bring-up.
        case 191: // getxattr
        case 192: // lgetxattr
        case 193: // fgetxattr
            ret = (U64)-K_ENODATA;
            break;
        case 194: // listxattr
        case 195: // llistxattr
        case 196: // flistxattr
            ret = 0; // empty attribute list
            break;
        case 188: // setxattr
        case 189: // lsetxattr
        case 190: // fsetxattr
        case 197: // removexattr
        case 198: // lremovexattr
        case 199: // fremovexattr
            ret = (U64)-K_ENOTSUP;
            break;
        case X64_SYS_lseek:
            ret = sys_lseek64(cpu, a1, a2, a3);
            break;
        case X64_SYS_pread64:
            ret = sys_pread64(cpu, a1, a2, a3, a4);
            break;
        case X64_SYS_readlink:
            ret = sys_readlink64(cpu, a1, a2, a3);
            break;
        case X64_SYS_readlinkat:
            // readlinkat(dirfd, path, buf, bufsiz). Modern glibc/wine ntdll use
            // THIS, not readlink(89), to resolve /proc/self/exe (the basis for
            // wine's loader/module dir derivation). dirfd is AT_FDCWD or the
            // path is absolute (/proc/self/exe is), so we can ignore dirfd and
            // reuse the readlink resolver on (path=a2, buf=a3, sz=a4).
            ret = sys_readlink64(cpu, a2, a3, a4);
            break;
        case X64_SYS_access:
        case X64_SYS_faccessat:
        case X64_SYS_faccessat2: {
            // access(path, mode) / faccessat(dirfd, path, mode[, flags]) /
            // faccessat2(dirfd, path, mode, flags). We don't model EUID perms
            // yet, so existence is the only check (mode/flags ignored). For the
            // *at forms the path is arg2 (dirfd is AT_FDCWD or the path is
            // absolute — wine probes ntdll.so by absolute path). wine uses
            // faccessat/faccessat2 to confirm its module dir before loading
            // ntdll, so without these it reports "cannot get path to ntdll.so".
            // Standalone runner (no KProcess) -> ENOENT, matching ld.so probes.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-2; break; }
            U64 pathArg = (nr == X64_SYS_access) ? a1 : a2;
            char path[1024] = {0};
            cpu->memory->memcpyFromGuest(path, pathArg, sizeof(path) - 1);
            std::shared_ptr<FsNode> n = Fs::getNodeFromLocalPath(
                cpu->thread->process->currentDirectory, BString::copy(path), true);
            ret = n ? 0 : (U64)-2;
            break;
        }
        case X64_SYS_mkdir:
        case X64_SYS_mkdirat: {
            // mkdir(path, mode) / mkdirat(dirfd, path, mode). wineboot creates
            // its prefix tree (WINEPREFIX, drive_c, windows, ...) through these.
            // path is arg1 (mkdir) or arg2 (mkdirat); dirfd is AT_FDCWD or the
            // path is absolute. Routes to the existing KProcess mkdir, which
            // writes into the native (writable) root overlay.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            U64 pathArg = (nr == X64_SYS_mkdir) ? a1 : a2;
            char path[1024] = {0};
            cpu->memory->memcpyFromGuest(path, pathArg, sizeof(path) - 1);
            BString full = Fs::getFullPath(cpu->thread->process->currentDirectory,
                                           BString::copy(path));
            ret = (U64)(S64)(S32)cpu->thread->process->mkdir(full);
            if (getenv("BW64_SYSTRACE")) {
                klog_fmt("sys_mkdir64: '%s' full='%s' -> %d", path, full.c_str(),
                         (int)(S32)ret);
            }
            break;
        }
        case X64_SYS_symlink:
        case X64_SYS_symlinkat: {
            // symlink(target, linkpath) / symlinkat(target, dirfd, linkpath).
            // wineboot builds dosdevices/c: -> ../drive_c and z: -> / symlinks
            // when initializing the prefix. target is arg1; linkpath is arg2
            // (symlink) or arg3 (symlinkat, dirfd=arg2 is AT_FDCWD/absolute).
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            char target[1024] = {0};
            char linkpath[1024] = {0};
            cpu->memory->memcpyFromGuest(target, a1, sizeof(target) - 1);
            U64 linkArg = (nr == X64_SYS_symlink) ? a2 : a3;
            cpu->memory->memcpyFromGuest(linkpath, linkArg, sizeof(linkpath) - 1);
            BString fullLink = Fs::getFullPath(cpu->thread->process->currentDirectory,
                                               BString::copy(linkpath));
            ret = (U64)(S64)(S32)cpu->thread->process->symlink(
                BString::copy(target), fullLink);
            if (getenv("BW64_SCDUMP")) {
                klog_fmt("sys_symlink64: target='%s' link='%s' (cwd='%s') -> %d",
                         target, fullLink.c_str(),
                         cpu->thread->process->currentDirectory.c_str(), (int)(S32)ret);
            }
            break;
        }
        case X64_SYS_link:
        case X64_SYS_linkat: {
            // link(oldpath, newpath) / linkat(olddirfd, oldpath, newdirfd,
            // newpath, flags). fontconfig builds its on-disk cache atomically by
            // writing a temp file, link()-ing it to the final cache name, then
            // unlink()-ing the temp — so a missing link() leaves the cache
            // perpetually un-built and fontconfig re-scans every font dir on
            // every process launch (the boot-storm font-scan loop). Route to
            // KProcess::link, which hard-links (or copies) in the writable
            // overlay. linkat's dirfds are AT_FDCWD/absolute in this path.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            U64 oldArg = (nr == X64_SYS_link) ? a1 : a2;
            U64 newArg = (nr == X64_SYS_link) ? a2 : a4;
            char oldp[1024] = {0};
            char newp[1024] = {0};
            cpu->memory->memcpyFromGuest(oldp, oldArg, sizeof(oldp) - 1);
            cpu->memory->memcpyFromGuest(newp, newArg, sizeof(newp) - 1);
            BString fullOld = Fs::getFullPath(cpu->thread->process->currentDirectory,
                                              BString::copy(oldp));
            BString fullNew = Fs::getFullPath(cpu->thread->process->currentDirectory,
                                              BString::copy(newp));
            ret = (U64)(S64)(S32)cpu->thread->process->link(fullOld, fullNew);
            if (getenv("BW64_SCDUMP")) {
                klog_fmt("sys_link64: old='%s' new='%s' -> %d",
                         fullOld.c_str(), fullNew.c_str(), (int)(S32)ret);
            }
            break;
        }
        case X64_SYS_rename:
        case X64_SYS_renameat:
        case X64_SYS_renameat2: {
            // rename(from, to) / renameat(olddirfd, from, newdirfd, to) /
            // renameat2(olddirfd, from, newdirfd, to, flags). wineserver saves
            // each registry branch by writing a temp file and rename()-ing it
            // into place (system.reg/user.reg/userdef.reg) — without this the
            // prefix never persists ("could not save registry branch ... :
            // Invalid argument"). Reuse KProcess::rename/renameat, which route
            // through the FsNode layer to the native rename in the writable
            // overlay. renameat2 with a plain (flags==0) rename is identical to
            // renameat; the NOREPLACE/EXCHANGE/WHITEOUT flag bits aren't
            // supported, so reject them with EINVAL (glibc/wine fall back to a
            // plain rename when renameat2 fails).
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            char from[1024] = {0};
            char to[1024] = {0};
            if (nr == X64_SYS_rename) {
                cpu->memory->memcpyFromGuest(from, a1, sizeof(from) - 1);
                cpu->memory->memcpyFromGuest(to,   a2, sizeof(to) - 1);
                ret = (U64)(S64)(S32)cpu->thread->process->rename(
                    BString::copy(from), BString::copy(to));
            } else {
                if (nr == X64_SYS_renameat2 && (a5 & ~0ULL) != 0) {
                    // any flag set -> unsupported in this path
                    ret = (U64)-K_EINVAL; break;
                }
                FD olddirfd = (FD)(S32)a1;
                FD newdirfd = (FD)(S32)a3;
                cpu->memory->memcpyFromGuest(from, a2, sizeof(from) - 1);
                cpu->memory->memcpyFromGuest(to,   a4, sizeof(to) - 1);
                ret = (U64)(S64)(S32)cpu->thread->process->renameat(
                    olddirfd, BString::copy(from), newdirfd, BString::copy(to));
            }
            if (getenv("BW64_SYSTRACE")) {
                klog_fmt("sys_rename64: '%s' -> '%s' ret=%d", from, to, (int)(S32)ret);
            }
            break;
        }
        case X64_SYS_socketpair: {
            // socketpair(domain, type, protocol, sv[2]). wineserver's first IPC
            // call — it creates the AF_UNIX request/reply channel. Reuse the
            // socket-object wiring (ksocketpairFds, no guest-memory access) and
            // write the two FDs into memory64 (pointer-free 4-byte ints, same
            // layout 32/64). `type` carries SOCK_CLOEXEC/NONBLOCK flag bits.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            U32 type = (U32)a2 & 0xFF;
            U32 sockFlags = (U32)a2 & ~0xFFu; // SOCK_CLOEXEC(0x80000)/NONBLOCK(0x800)
            U32 passFlags = 0;
            if (sockFlags & 0x80000) passFlags |= K_O_CLOEXEC;
            if (sockFlags & 0x800)   passFlags |= K_O_NONBLOCK;
            FD fd1 = 0, fd2 = 0;
            S32 rc = ksocketpairFds(cpu->thread, (U32)a1, type, (U32)a3, passFlags, fd1, fd2);
            if (rc) {
                kwarnSocketCreateFailure(cpu->thread, (U32)a1, (U32)a2, (U32)a3, rc, "socketpair");
                ret = (U64)(S64)rc;
                break;
            }
            cpu->memory->writed(a4, (U32)fd1);
            cpu->memory->writed(a4 + 4, (U32)fd2);
            ret = 0;
            break;
        }
        case X64_SYS_shutdown:
            // shutdown(sockfd, how). No guest memory; reuse the 32-bit handler.
            // wineserver's sock_check_pollhup shuts down one end of its pair to
            // probe POLLHUP behaviour during init.
            if (!cpu->thread) { ret = (U64)-K_ENOSYS; break; }
            ret = (U64)(S64)(S32)kshutdown(cpu->thread, (U32)a1, (U32)a2);
            break;
        case X64_SYS_socket: {
            // socket(domain, type, protocol). wineserver creates its listening
            // AF_UNIX socket. ksocket takes no guest memory — call directly. The
            // type's high bits are SOCK_CLOEXEC(0x80000)/SOCK_NONBLOCK(0x800);
            // strip them for ksocket and apply via fcntl after.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            U32 sockType = (U32)a2 & 0xFF;
            FD sfd = (FD)(S32)ksocket((U32)a1, sockType, (U32)a3);
            kwarnSocketCreateFailure(cpu->thread, (U32)a1, (U32)a2, (U32)a3, (S32)sfd, "socket");
            if ((S32)sfd >= 0) {
                if ((U32)a2 & 0x80000) cpu->thread->process->fcntrl(cpu->thread, sfd, K_F_SETFD, FD_CLOEXEC);
                if ((U32)a2 & 0x800)   cpu->thread->process->fcntrl(cpu->thread, sfd, K_F_SETFL, K_O_NONBLOCK);
            }
            ret = (U64)(S64)(S32)sfd;
            break;
        }
        case X64_SYS_bind: {
            // bind(sockfd, addr, addrlen) — wineserver binds its listening
            // AF_UNIX socket (wineserver.sock). Bounce the sockaddr into 32-bit
            // scratch and reuse kbind + the object layer.
            if (!cpu->thread) { ret = (U64)-K_ENOSYS; break; }
            U32 s32 = bounceSockaddrTo32(cpu, a2, (U32)a3, nullptr);
            if (!s32) { ret = (U64)-K_EFAULT; break; }
            ret = (U64)(S64)(S32)kbind(cpu->thread, (U32)a1, s32, (U32)a3);
            break;
        }
        case X64_SYS_connect: {
            // connect(sockfd, addr, addrlen) — wine64 connects to wineserver's
            // socket. Same bounce.
            if (!cpu->thread) { ret = (U64)-K_ENOSYS; break; }
            U32 s32 = bounceSockaddrTo32(cpu, a2, (U32)a3, nullptr);
            if (!s32) { ret = (U64)-K_EFAULT; break; }
            ret = (U64)(S64)(S32)kconnect(cpu->thread, (U32)a1, s32, (U32)a3);
            if (getenv("BW64_SCDUMP")) {
                // sockaddr_un: family@0 (2), sun_path@2. sockaddr_in: family@0,
                // port@2 (BE u16), addr@4 (BE u32). Decode both so we can tell a
                // unix X11 connect (/tmp/.X11-unix/X0) from a TCP X connect
                // (port 6000+display) from plain DNS.
                U16 fam = cpu->memory->readw(a2);
                char detail[128] = {0};
                if (fam == 1 /*AF_UNIX*/) {
                    cpu->memory->memcpyFromGuest(detail, a2 + 2, sizeof(detail) - 1);
                } else if (fam == 2 /*AF_INET*/) {
                    U16 portBE = cpu->memory->readw(a2 + 2);
                    U32 ipBE   = cpu->memory->readd(a2 + 4);
                    U16 port = (U16)((portBE >> 8) | (portBE << 8));
                    snprintf(detail, sizeof(detail), "%u.%u.%u.%u:%u",
                             (ipBE) & 0xff, (ipBE >> 8) & 0xff,
                             (ipBE >> 16) & 0xff, (ipBE >> 24) & 0xff, port);
                } else {
                    snprintf(detail, sizeof(detail), "(family %d)", (int)fam);
                }
                klog_fmt("sys_connect64: fd=%d family=%d %s -> %d",
                         (int)a1, (int)fam, detail, (int)(S32)ret);
            }
            break;
        }
        case X64_SYS_listen:
            // listen(sockfd, backlog) — no guest memory.
            if (!cpu->thread) { ret = (U64)-K_ENOSYS; break; }
            ret = (U64)(S64)(S32)klisten(cpu->thread, (U32)a1, (U32)a2);
            break;
        case X64_SYS_accept: {
            // accept(sockfd, addr, addrlen) — wineserver accepts a wine64 client.
            // addr/addrlen are optional out-params; pass a scratch slot when set
            // and copy the result back to memory64.
            if (!cpu->thread) { ret = (U64)-K_ENOSYS; break; }
            U32 lenSlot = 0;
            U32 s32 = a2 ? bounceSockaddrTo32(cpu, 0, 256, &lenSlot) : 0;
            ret = (U64)(S64)(S32)kaccept(cpu->thread, (U32)a1, s32, lenSlot, 0);
            // (peer sockaddr writeback omitted — AF_UNIX accept returns an empty
            // name; wine doesn't rely on it for the server connection.)
            break;
        }
        case X64_SYS_getsockname:
        case X64_SYS_getpeername: {
            // get{sock,peer}name(fd, addr, addrlen*). Bounce; copy the resulting
            // sockaddr + length back into memory64.
            if (!cpu->thread || !a2 || !a3) { ret = (U64)-K_EFAULT; break; }
            U32 inLen = cpu->memory->readd(a3);
            U32 lenSlot = 0;
            U32 s32 = bounceSockaddrTo32(cpu, 0, inLen, &lenSlot);
            if (!s32) { ret = (U64)-K_EFAULT; break; }
            U32 rc = (nr == X64_SYS_getsockname)
                ? kgetsockname(cpu->thread, (U32)a1, s32, lenSlot)
                : kgetpeername(cpu->thread, (U32)a1, s32, lenSlot);
            if ((S32)rc >= 0) {
                U32 outLen = cpu->thread->memory->readd(lenSlot);
                if (outLen > 256) outLen = 256;
                U8 tmp[256];
                cpu->thread->memory->memcpy(tmp, s32, outLen);
                cpu->memory->memcpyToGuest(a2, tmp, outLen);
                cpu->memory->writed(a3, outLen);
            }
            ret = (U64)(S64)(S32)rc;
            break;
        }
        case X64_SYS_fchdir:
            // fchdir(fd) — chdir to a directory held open by descriptor. No
            // guest memory. wineserver fchdir's into the prefix's server dir;
            // without it, it mapped ENOSYS to a file error and asserted.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            ret = (U64)(S64)(S32)cpu->thread->process->fchdir((FD)a1);
            break;
        case X64_SYS_ftruncate:
            // ftruncate(fd, length) — wineserver sizes its registry/mapping
            // files. No guest memory.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            ret = (U64)(S64)(S32)cpu->thread->process->ftruncate64((FD)a1, a2);
            break;
        case X64_SYS_umask:
            // umask(mask) — returns the previous mask. wine sets it during init.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            ret = (U64)cpu->thread->process->umask((U32)a1);
            break;
        case X64_SYS_setsockopt: {
            // setsockopt(fd, level, name, optval, optlen). wineserver sets
            // socket options (SO_PASSCRED etc.) on its connection. optval is a
            // small pointer-free buffer — bounce it into 32-bit scratch.
            if (!cpu->thread) { ret = (U64)-K_ENOSYS; break; }
            U32 optlen = (U32)a5;
            U32 v32 = (a4 && optlen) ? bounceSockaddrTo32(cpu, a4, optlen, nullptr) : 0;
            ret = (U64)(S64)(S32)ksetsockopt(cpu->thread, (U32)a1, (U32)a2, (U32)a3, v32, optlen);
            break;
        }
        case X64_SYS_getsockopt: {
            // getsockopt(fd, level, name, optval, optlen*). optlen is a pointer
            // to socklen_t (in/out). Bounce optval + the length slot, run the
            // helper, copy the result + new length back to memory64.
            if (!cpu->thread || !a5) { ret = (U64)-K_EFAULT; break; }
            U32 inLen = cpu->memory->readd(a5);
            U32 lenSlot = 0;
            U32 v32 = bounceSockaddrTo32(cpu, a4, inLen, &lenSlot);
            if (!v32) { ret = (U64)-K_EFAULT; break; }
            U32 rc = kgetsockopt(cpu->thread, (U32)a1, (U32)a2, (U32)a3, v32, lenSlot);
            if ((S32)rc >= 0) {
                U32 outLen = cpu->thread->memory->readd(lenSlot);
                if (outLen > 256) outLen = 256;
                U8 tmp[256];
                cpu->thread->memory->memcpy(tmp, v32, outLen);
                cpu->memory->memcpyToGuest(a4, tmp, outLen);
                cpu->memory->writed(a5, outLen);
            }
            ret = (U64)(S64)(S32)rc;
            break;
        }
        case X64_SYS_sendmsg:
            // sendmsg(fd, msghdr*, flags) — the wineserver request channel.
            ret = sys_sendmsg64(cpu, a1, a2, a3);
            break;
        case X64_SYS_recvmsg:
            // recvmsg(fd, msghdr*, flags) — the wineserver reply channel
            // (carries SCM_RIGHTS fds for shared mappings).
            ret = sys_recvmsg64(cpu, a1, a2, a3);
            break;
        case X64_SYS_sendto:
        case X64_SYS_recvfrom: {
            // sendto(fd, buf, len, flags, dest_addr, addrlen) /
            // recvfrom(fd, buf, len, flags, src_addr, addrlen). wine's unix ntdll
            // uses these on the (connected AF_UNIX) wineserver socket, so the
            // addr argument is unused — pass NULL addr to k*. Bounce the data
            // buffer through the 32-bit msg scratch (k* read/write thread->memory)
            // and copy results back to the 64-bit guest buffer via cpu->memory.
            if (!cpu->thread || !cpu->thread->process || !cpu->thread->memory) {
                ret = (U64)-K_ENOSYS; break;
            }
            U32 scratch = msgScratch(cpu->thread);
            if (!scratch) { ret = (U64)-K_EFAULT; break; }
            U32 dataAddr = scratch + MSG_SCRATCH_DATA;
            U32 cap = MSG_SCRATCH_BYTES - MSG_SCRATCH_DATA;
            U64 len = a3; if (len > cap) len = cap;
            if (nr == X64_SYS_sendto) {
                U8 tmp[4096];
                U64 off = 0;
                // copy guest buffer -> 32-bit scratch in chunks
                while (off < len) {
                    U32 chunk = (U32)((len - off > sizeof(tmp)) ? sizeof(tmp) : (len - off));
                    cpu->memory->memcpyFromGuest(tmp, a2 + off, chunk);
                    cpu->thread->memory->memcpy(dataAddr + (U32)off, tmp, chunk);
                    off += chunk;
                }
                ret = (U64)(S64)(S32)ksendto(cpu->thread, (U32)a1, dataAddr, (U32)len,
                                             (U32)a4, 0, 0);
            } else {
                S32 rc = (S32)krecvfrom(cpu->thread, (U32)a1, dataAddr, (U32)len,
                                        (U32)a4, 0, 0);
                if (rc > 0) {
                    U8 tmp[4096];
                    U64 off = 0;
                    while (off < (U64)rc) {
                        U32 chunk = (U32)(((U64)rc - off > sizeof(tmp)) ? sizeof(tmp) : ((U64)rc - off));
                        cpu->thread->memory->memcpy(tmp, dataAddr + (U32)off, chunk);
                        cpu->memory->memcpyToGuest(a2 + off, tmp, chunk);
                        off += chunk;
                    }
                }
                ret = (U64)(S64)rc;
            }
            break;
        }
        case X64_SYS_sendmmsg:
        case X64_SYS_recvmmsg: {
            // sendmmsg(fd, mmsghdr* msgvec, vlen, flags[, timeout]) /
            // recvmmsg(...). wine's unix ntdll uses sendmmsg on the wineserver
            // socket. struct mmsghdr = { struct msghdr msg_hdr; unsigned msg_len }:
            // the x86-64 msghdr is 56 bytes, msg_len at +56, stride 64 (8-byte
            // aligned). Loop the existing single-message 64-bit path over each
            // entry, writing back the per-message byte count, and return the
            // number of messages processed (Linux semantics). Stop early on the
            // first error after >=1 success (like the kernel).
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            U64 fd = a1, vec = a2; U32 vlen = (U32)a3; U64 flags = a4;
            const U64 MMSG_STRIDE = 64;   // sizeof(struct mmsghdr) on x86-64
            const U64 MSGLEN_OFF  = 56;   // offsetof(mmsghdr, msg_len)
            U32 done = 0;
            S64 lastErr = 0;
            for (U32 i = 0; i < vlen; i++) {
                U64 hdr = vec + i * MMSG_STRIDE;
                S64 rc = (nr == X64_SYS_sendmmsg)
                       ? (S64)(S32)sys_sendmsg64(cpu, fd, hdr, flags)
                       : (S64)(S32)sys_recvmsg64(cpu, fd, hdr, flags);
                if (rc < 0) { lastErr = rc; break; }
                cpu->memory->writed(hdr + MSGLEN_OFF, (U32)rc);
                done++;
            }
            ret = done ? (U64)done : (U64)lastErr;
            break;
        }
        case X64_SYS_setsid:
            // setsid() — new session; we don't model sessions, return the pid
            // (matches the 32-bit stub). wineserver daemonizes with this.
            ret = (cpu->thread && cpu->thread->process) ? (U64)cpu->thread->process->id : 1;
            break;
        case X64_SYS_unlink:
        case X64_SYS_unlinkat: {
            // unlink(path) / unlinkat(dirfd, path, flags). wine removes stale
            // lock/socket files in its prefix. path is arg1 (unlink) or arg2
            // (unlinkat, dirfd AT_FDCWD/absolute). Route to KProcess::unlinkFile.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            U64 pathArg = (nr == X64_SYS_unlink) ? a1 : a2;
            char path[1024] = {0};
            cpu->memory->memcpyFromGuest(path, pathArg, sizeof(path) - 1);
            ret = (U64)(S64)(S32)cpu->thread->process->unlinkFile(
                BString(Fs::getFullPath(cpu->thread->process->currentDirectory,
                                        BString::copy(path))));
            break;
        }
        case X64_SYS_pipe:
        case X64_SYS_pipe2: {
            // pipe(fds) / pipe2(fds, flags). Boxedwine models pipes as a
            // connected AF_UNIX stream pair (same as the 32-bit path). wineserver
            // uses a pipe for its signal/wakeup fd. Write the two FDs to memory64.
            // EFAULT before the process check: the bare --x64-selftest runner
            // has no KProcess, and the pipe(NULL) self-test expects -EFAULT.
            if (a1 == 0) { ret = (U64)-K_EFAULT; break; }  // pipefd must be valid
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            U32 pipeFlags = (nr == X64_SYS_pipe2) ? (U32)a2 : 0;
            U32 passFlags = 0;
            if (pipeFlags & 0x80000) passFlags |= K_O_CLOEXEC;  // O_CLOEXEC
            if (pipeFlags & 0x800)   passFlags |= K_O_NONBLOCK; // O_NONBLOCK
            FD fd1 = 0, fd2 = 0;
            S32 rc = ksocketpairFds(cpu->thread, K_AF_UNIX, K_SOCK_STREAM, 0, passFlags, fd1, fd2);
            if (rc) { ret = (U64)(S64)rc; break; }
            cpu->memory->writed(a1, (U32)fd1);
            cpu->memory->writed(a1 + 4, (U32)fd2);
            ret = 0;
            break;
        }
        case X64_SYS_uname:
            ret = sys_uname64(cpu, a1);
            break;
        case X64_SYS_getrandom:
            ret = sys_getrandom64(cpu, a1, a2, a3);
            break;
        case X64_SYS_getcpu:
            ret = sys_getcpu64(cpu, a1, a2, a3);
            break;
        case X64_SYS_memfd_create: {
            // memfd_create(name, flags). wine's ntdll uses this for its
            // cross-process shared sections (and libpulse for its shm pool);
            // without it those initializations fail with ENOSYS and the
            // client can block forever. The name is a guest pointer, so read
            // it through the 64-bit memory interface.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            char name[256] = {0};
            if (a1) cpu->memory->memcpyFromGuest(name, a1, sizeof(name) - 1);
            ret = (U64)(S64)(S32)cpu->thread->process->memfd_create(BString::copy(name), (U32)a2);
            break;
        }
        case X64_SYS_timerfd_create:
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            ret = (U64)(S64)(S32)cpu->thread->process->timerfd_create((U32)a1, (U32)a2);
            break;
        case X64_SYS_timerfd_settime:
        case X64_SYS_timerfd_gettime: {
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            KProcessPtr process = cpu->thread->process;
            KFileDescriptorPtr fd = process->getFileDescriptor((FD)a1);
            if (!fd) { ret = (U64)-K_EBADF; break; }
            std::shared_ptr<KTimer> timer = std::dynamic_pointer_cast<KTimer>(fd->kobject);
            if (!timer) { ret = (U64)-K_EINVAL; break; }
            if (nr == X64_SYS_timerfd_gettime) {
                if (!a2) { ret = (U64)-K_EFAULT; break; }
                const U64 interval = timer->getMicroInterval();
                const U64 next = timer->getMicroNextTimer();
                const U64 now = timer->getClockNowMicroseconds();
                const U64 remaining = next > now ? next - now : 0;
                cpu->memory->writeq(a2, interval / 1000000);
                cpu->memory->writeq(a2 + 8, (interval % 1000000) * 1000);
                cpu->memory->writeq(a2 + 16, remaining / 1000000);
                cpu->memory->writeq(a2 + 24, (remaining % 1000000) * 1000);
                ret = 0;
                break;
            }
            constexpr U32 TFD_TIMER_ABSTIME = 1;
            if ((a2 & ~TFD_TIMER_ABSTIME) != 0) { ret = (U64)-K_EINVAL; break; }
            if (!a3) { ret = (U64)-K_EFAULT; break; }
            const S64 intervalSec = (S64)cpu->memory->readq(a3);
            const S64 intervalNsec = (S64)cpu->memory->readq(a3 + 8);
            const S64 valueSec = (S64)cpu->memory->readq(a3 + 16);
            const S64 valueNsec = (S64)cpu->memory->readq(a3 + 24);
            if (intervalSec < 0 || valueSec < 0 || intervalNsec < 0 || intervalNsec >= 1000000000 ||
                valueNsec < 0 || valueNsec >= 1000000000 ||
                (U64)intervalSec > UINT64_MAX / 1000000 || (U64)valueSec > UINT64_MAX / 1000000) {
                ret = (U64)-K_EINVAL;
                break;
            }
            if (a4) {
                const U64 interval = timer->getMicroInterval();
                const U64 next = timer->getMicroNextTimer();
                const U64 now = timer->getClockNowMicroseconds();
                const U64 remaining = next > now ? next - now : 0;
                cpu->memory->writeq(a4, interval / 1000000);
                cpu->memory->writeq(a4 + 8, (interval % 1000000) * 1000);
                cpu->memory->writeq(a4 + 16, remaining / 1000000);
                cpu->memory->writeq(a4 + 24, (remaining % 1000000) * 1000);
            }
            const U64 interval = (U64)intervalSec * 1000000 + (U64)intervalNsec / 1000;
            U64 next = (U64)valueSec * 1000000 + (U64)valueNsec / 1000;
            if (next && !(a2 & TFD_TIMER_ABSTIME)) next += timer->getClockNowMicroseconds();
            timer->setTimes(interval, next);
            ret = 0;
            break;
        }
        case X64_SYS_prlimit64:
            ret = sys_prlimit64_64(cpu, a1, a2, a3, a4);
            break;
        case X64_SYS_getrlimit:
            // getrlimit(resource, struct rlimit* {unsigned long cur,max}).
            // wine's unix ntdll queries RLIMIT_STACK/NOFILE/AS during thread
            // setup. Pretend "no limit" (RLIM_INFINITY) like prlimit64 does.
            if (a2) {
                cpu->memory->writeq(a2, ~0ULL);
                cpu->memory->writeq(a2 + 8, ~0ULL);
            }
            ret = 0;
            break;
        case X64_SYS_setrlimit:
            ret = 0; // accept and ignore — we don't enforce limits
            break;
        case X64_SYS_sched_getscheduler:
            ret = 0; // SCHED_OTHER
            break;
        case X64_SYS_sched_setscheduler:
            ret = 0; // accept; we have one scheduling class
            break;
        case X64_SYS_sched_getparam:
            // sched_param { int sched_priority } — report priority 0.
            if (a2) cpu->memory->writed(a2, 0);
            ret = 0;
            break;
        case X64_SYS_mlockall:
        case X64_SYS_munlockall:
        case X64_SYS_mlock:
        case X64_SYS_munlock:
            // We never page guest memory out, so locking is a no-op success.
            ret = 0;
            break;
        case X64_SYS_clock_gettime:
            ret = sys_clock_gettime64(cpu, a1, a2);
            break;
        case X64_SYS_getuid:
        case X64_SYS_geteuid:
        case X64_SYS_getgid:
        case X64_SYS_getegid:
            ret = 1000; // pretend uid/gid 1000
            break;
        case X64_SYS_setuid:
        case X64_SYS_setgid:
            // We model exactly one user (uid/gid 1000). Tools like coreutils
            // ls/id call setuid/setgid to drop privileges at startup; accept
            // any request to set our own id and reject others with EPERM, the
            // same way a real kernel would for an unprivileged process.
            ret = (a1 == 1000) ? 0 : (U64)-1; // -EPERM
            break;
        case 118: // getresuid(ruid*, euid*, suid*)
        case 120: // getresgid(rgid*, egid*, sgid*)
            // wine's ntdll queries these during process init. We model one
            // user, so real==effective==saved==1000. Each arg is a uid_t* (4
            // bytes); a NULL pointer would be EFAULT but wine always passes
            // valid stack slots.
            if (!a1 || !a2 || !a3) { ret = (U64)-K_EFAULT; break; }
            cpu->memory->writed(a1, 1000);
            cpu->memory->writed(a2, 1000);
            cpu->memory->writed(a3, 1000);
            ret = 0;
            break;
        case 117: // setresuid(ruid, euid, suid)
        case 119: // setresgid(rgid, egid, sgid)
            // Accept setting our own id (1000 or -1 = "unchanged"); reject any
            // attempt to switch to a different user with EPERM, mirroring an
            // unprivileged process. -1 (0xffffffff) means leave that id alone.
            {
                auto ok = [](U64 v){ return v == 1000 || v == 0xffffffffULL; };
                ret = (ok(a1) && ok(a2) && ok(a3)) ? 0 : (U64)-1; // -EPERM
            }
            break;
        case X64_SYS_time: {
            // time(tloc): seconds since epoch; writes to *tloc when non-null.
            U64 sec = KSystem::getSystemTimeAsMicroSeconds() / 1000000ULL;
            if (a1) cpu->memory->writeq(a1, sec);
            ret = sec;
            break;
        }
        case X64_SYS_getpid:
        case X64_SYS_gettid:
            ret = cpu->thread ? cpu->thread->id : 1;
            break;
        case X64_SYS_futex:
            ret = sys_futex64(cpu, a1, (U32)a2, (U32)a3, a4, a5, (U32)a6);
            break;
        case X64_SYS_poll:
        case X64_SYS_ppoll: {
            // poll(fds, nfds, timeout_ms) / ppoll(fds, nfds, timespec*, sigmask).
            // wineserver's sock_init runs sock_check_pollhup(): it closes one end
            // of a socketpair and polls the other, requiring POLLHUP — the old
            // stub (return 0) failed that check and aborted server startup. Bounce
            // the pollfd array (8 bytes each: {int fd; short events; short revents}
            // — layout-identical 32/64) into the process's 32-bit scratch KMemory,
            // call the width-agnostic kpoll (which drives internal_poll's real
            // readiness/blocking + POLLHUP detection), then copy the array back so
            // the updated revents reach the 64-bit guest.
            if (!cpu->thread || !cpu->thread->process || !cpu->thread->memory) {
                ret = (U64)-K_ENOSYS; break;
            }
            U32 nfds = (U32)a2;
            // kpoll's timeout convention (see internal_poll): 0 == return
            // immediately (non-blocking); a value > 0xF0000000 == wait forever;
            // anything in between is a millisecond deadline. The userspace poll
            // ABI maps onto this directly — poll(timeout=-1) is 0xFFFFFFFF
            // (>0xF0000000 == infinite) and poll(timeout=0) is non-blocking — so
            // the 32-bit path passes the raw arg straight through. We do the same.
            U32 timeoutMs;
            if (nr == X64_SYS_ppoll) {
                // ppoll: a3 is a timespec* (NULL == infinite wait).
                if (a3 == 0) {
                    timeoutMs = 0xFFFFFFFFu; // infinite
                } else {
                    U64 sec  = cpu->memory->readq(a3);
                    U64 nsec = cpu->memory->readq(a3 + 8);
                    U64 ms = sec * 1000 + nsec / 1000000;
                    // A real zero timespec is a non-blocking poll (kpoll: 0).
                    timeoutMs = (U32)ms;
                }
            } else {
                // poll: a3 is a signed int ms; pass through (-1 -> 0xFFFFFFFF).
                timeoutMs = (U32)a3;
            }
            if (nfds == 0) {
                // No fds: kpoll with our scratch still honors the timeout sleep.
                U32 scratch0 = bounceSockaddrTo32(cpu, 0, 0, nullptr);
                ret = (U64)(S64)(S32)kpoll(cpu->thread, scratch0, 0, timeoutMs);
                break;
            }
            U32 bytes = nfds * 8;
            // Reuse the multi-page socket scratch (>= one page); cap nfds so the
            // array fits comfortably within a single page.
            if (bytes > K_PAGE_SIZE) { ret = (U64)-K_EINVAL; break; }
            U32 scratch = bounceSockaddrTo32(cpu, 0, 0, nullptr);
            if (!scratch) { ret = (U64)-K_EFAULT; break; }
            // Copy the guest pollfd array into the 32-bit scratch.
            {
                U8 tmp[K_PAGE_SIZE];
                cpu->memory->memcpyFromGuest(tmp, a1, bytes);
                cpu->thread->memory->memcpy(scratch, tmp, bytes);
            }
            S32 rc = (S32)kpoll(cpu->thread, scratch, nfds, timeoutMs);
            // Copy the (revents-updated) array back to the 64-bit guest.
            {
                U8 tmp[K_PAGE_SIZE];
                cpu->thread->memory->memcpy(tmp, scratch, bytes);
                cpu->memory->memcpyToGuest(a1, tmp, bytes);
            }
            ret = (U64)(S64)rc;
            break;
        }
        case X64_SYS_pselect6: {
            // pselect6(nfds, readfds*, writefds*, exceptfds*, timespec*, sigmask).
            // We have no native select, but kpoll() is the real readiness/blocking
            // engine. Translate the fd_set bitmaps into a pollfd array, run kpoll,
            // then translate the revents back into the fd_sets. Without this, wine
            // (winex11/msg-loop) spins on pselect6=ENOSYS and never reaches GL.
            if (!cpu->thread || !cpu->thread->process || !cpu->thread->memory) {
                ret = (U64)-K_ENOSYS; break;
            }
            U32 nfds = (U32)a1;          // highest fd + 1
            U64 rfdsAddr = a2, wfdsAddr = a3, efdsAddr = a4, tsAddr = a5;
            if (nfds > 1024) { ret = (U64)-K_EINVAL; break; }

            // Timeout: a5 is a timespec* (NULL == infinite). Match kpoll's
            // convention (0 == non-blocking, >0xF0000000 == infinite).
            U32 timeoutMs;
            if (tsAddr == 0) {
                timeoutMs = 0xFFFFFFFFu;
            } else {
                U64 sec  = cpu->memory->readq(tsAddr);
                U64 nsec = cpu->memory->readq(tsAddr + 8);
                timeoutMs = (U32)(sec * 1000 + nsec / 1000000);
            }

            // Read the three fd_set bitmaps (each (nfds+7)/8 bytes, capped to the
            // 128-byte/1024-fd standard). 0 addr == that set not requested.
            U32 setBytes = (nfds + 7) / 8;
            if (setBytes > 128) setBytes = 128;
            U8 rset[128] = {0}, wset[128] = {0}, eset[128] = {0};
            if (rfdsAddr) cpu->memory->memcpyFromGuest(rset, rfdsAddr, setBytes);
            if (wfdsAddr) cpu->memory->memcpyFromGuest(wset, wfdsAddr, setBytes);
            if (efdsAddr) cpu->memory->memcpyFromGuest(eset, efdsAddr, setBytes);

            auto isSet = [](const U8* s, U32 fd) { return (s[fd >> 3] >> (fd & 7)) & 1; };

            // Build a pollfd array (8 bytes each: {int fd; short events; short revents})
            // — one entry per fd that appears in any set. POLLIN=1, POLLOUT=4,
            // POLLPRI=2 (the select events) per Linux poll.h.
            struct PFD { S32 fd; U16 ev; U16 rev; };
            PFD pfds[1024];
            U32 npoll = 0;
            for (U32 fd = 0; fd < nfds && npoll < 1024; fd++) {
                U16 ev = 0;
                if (rfdsAddr && isSet(rset, fd)) ev |= 0x0001; // POLLIN
                if (wfdsAddr && isSet(wset, fd)) ev |= 0x0004; // POLLOUT
                if (efdsAddr && isSet(eset, fd)) ev |= 0x0002; // POLLPRI
                if (ev) { pfds[npoll].fd = (S32)fd; pfds[npoll].ev = ev; pfds[npoll].rev = 0; npoll++; }
            }

            // Run kpoll via the 32-bit scratch (same path the poll/ppoll case uses).
            S32 rc;
            U32 scratch = bounceSockaddrTo32(cpu, 0, 0, nullptr);
            if (!scratch) { ret = (U64)-K_EFAULT; break; }
            if (npoll == 0) {
                rc = (S32)kpoll(cpu->thread, scratch, 0, timeoutMs);
            } else {
                U8 tmp[K_PAGE_SIZE] = {0};
                for (U32 i = 0; i < npoll; i++) {
                    U8* e = tmp + i * 8;
                    *(U32*)e = (U32)pfds[i].fd;
                    *(U16*)(e + 4) = pfds[i].ev;
                    *(U16*)(e + 6) = 0;
                }
                cpu->thread->memory->memcpy(scratch, tmp, npoll * 8);
                rc = (S32)kpoll(cpu->thread, scratch, npoll, timeoutMs);
                cpu->thread->memory->memcpy(tmp, scratch, npoll * 8);
                for (U32 i = 0; i < npoll; i++) pfds[i].rev = *(U16*)(tmp + i * 8 + 6);
            }
            if (rc < 0) { ret = (U64)(S64)rc; break; }

            // Translate revents back into freshly-zeroed fd_sets and count ready fds
            // (select returns the total number of ready bits across all sets).
            U8 outR[128] = {0}, outW[128] = {0}, outE[128] = {0};
            U32 readyCount = 0;
            for (U32 i = 0; i < npoll; i++) {
                U32 fd = (U32)pfds[i].fd; U16 rev = pfds[i].rev;
                if (rfdsAddr && (rev & (0x0001 | 0x0010 /*POLLHUP*/ ))) { outR[fd>>3] |= (1<<(fd&7)); readyCount++; }
                if (wfdsAddr && (rev & 0x0004)) { outW[fd>>3] |= (1<<(fd&7)); readyCount++; }
                if (efdsAddr && (rev & (0x0002 | 0x0008 /*POLLERR*/))) { outE[fd>>3] |= (1<<(fd&7)); readyCount++; }
            }
            if (rfdsAddr) cpu->memory->memcpyToGuest(rfdsAddr, outR, setBytes);
            if (wfdsAddr) cpu->memory->memcpyToGuest(wfdsAddr, outW, setBytes);
            if (efdsAddr) cpu->memory->memcpyToGuest(efdsAddr, outE, setBytes);
            ret = (U64)readyCount;
            break;
        }
        case X64_SYS_sched_yield:
            // Relinquish the host CPU so a sibling guest thread/process that must
            // run to signal an awaited event isn't starved while this thread
            // spin-then-yields (RtlpWaitForCriticalSection, a server_select retry,
            // or wine's NtDelayExecution short-sleep loop).
            //
            // In the MULTI-THREADED build each guest thread runs on its own host
            // thread, so std::this_thread::yield() is the whole job. We must NOT
            // set cpu->yield here: the MT run loop (normalPlatformMultiThreaded.cpp)
            // treats `yield==true && !terminating` as "this thread is done" and
            // BREAKS out — deleting the thread. That made any guest sched_yield
            // (notably wine's Sleep() short-delay spin) silently destroy the
            // calling thread mid-run -> the process went idle/hung. cpu->yield is
            // only meaningful for the single-threaded cooperative scheduler.
#ifndef BOXEDWINE_MULTI_THREADED
            cpu->yield = true;
#endif
            std::this_thread::yield();
            ret = 0;
            break;
        case X64_SYS_sched_getaffinity:
            ret = sys_sched_getaffinity64(cpu, a1, a2, a3);
            break;
        case X64_SYS_sched_setaffinity:
            ret = sys_sched_setaffinity64(cpu, a1, a2, a3);
            break;
        case X64_SYS_kill: {
            // kill(pid, sig). For a real target pid>0 route through the shared
            // KSystem::kill so it consults the live process table — wineserver
            // probes client liveness with kill(pid,0) and force-reaps dead
            // clients with kill(pid,SIGKILL) during the multi-process boot
            // teardown. The old "single-process world" stub answered kill(pid,0)
            // as ALWAYS-alive (sig==0 short-circuited to 0 for ANY pid) and
            // kill(otherpid,sig) as ALWAYS -ESRCH, so wineserver could neither
            // detect a dead client nor kill it -> its reap state machine wedged
            // and double-freed a process object (the "unsorted double linked
            // list"/release_object heap corruption during boot-helper exit).
            // KSystem::kill returns -ESRCH for a nonexistent pid and 0 for a
            // live one (sig==0), matching POSIX and the 32-bit syscall_kill.
            U32 sig = (U32)a2;
            S64 spid = (S64)a1;
            U64 ourPid = cpu->thread && cpu->thread->process ?
                         (U64)cpu->thread->process->id : 1;
            if (spid > 0 && (U64)spid != ourPid) {
                ret = (U64)(S64)(S32)KSystem::kill((S32)spid, sig);
                break;
            }
            // pid<=0 (process group / broadcast) or our own pid: deliver to self
            // synchronously, as before. sig==0 is a permission/liveness probe.
            if (sig == 0) { ret = 0; break; }
            if (deliverSignalSync(cpu, sig)) {
                ret = 0;
            } else {
                klog_fmt("ksyscall64: kill self sig=%u — no handler installed", sig);
                ret = 0;
            }
            break;
        }
        case X64_SYS_rt_sigtimedwait:
            ret = sys_rt_sigtimedwait64(cpu, a1, a2, a3, a4);
            break;
        case X64_SYS_rt_sigsuspend:
            ret = sys_rt_sigsuspend64(cpu, a1, a2);
            break;
        case X64_SYS_rt_sigpending:
            ret = sys_rt_sigpending64(cpu, a1, a2);
            break;
        case X64_SYS_rt_sigqueueinfo:
            // rt_sigqueueinfo(tgid, sig, info) — like kill but with siginfo.
            // We don't deliver, so report success the same way kill does.
            ret = 0;
            break;
        case X64_SYS_pause:
            // pause() blocks till any signal; we never deliver, so the
            // glibc-compatible answer is -EINTR (caller's loop retries).
            ret = (U64)-K_EINTR;
            break;
        case X64_SYS_wait4: {
            // wait4(pid, status*, options, rusage*) — reap a forked child (e.g.
            // wineboot waiting on the wineserver it fork+exec'd). Reuse the
            // arch-neutral reaper and write the status into memory64. rusage
            // (a4) is ignored.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ECHILD; break; }
            int status = 0;
            U32 rc = KSystem::reapChild(cpu->thread, (S32)a1, (U32)a3, a2 ? &status : nullptr);
            if ((S32)rc > 0 && a2) {
                cpu->memory->writed(a2, (U32)status);
            }
            ret = (U64)(S64)(S32)rc;
            break;
        }
        case X64_SYS_clone:
            // x86-64 clone arg order: (flags=rdi, stack=rsi, parent_tid=rdx,
            // child_tid=r10, tls=r8). Create a real thread sharing this
            // process + memory64. Falls back to -ENOSYS for non-thread clones.
            if (cpu->thread && cpu->thread->process && cpu->thread->process->memory64) {
                ret = cpu->thread->process->clone64(cpu->thread, a1, a2, a3, a5, a4);
            } else {
                ret = unimplementedSyscall64(nr);
            }
            break;
        case X64_SYS_execve:
            // Replaces the process image. On success returns -K_CONTINUE (the
            // new program's RAX is already set by loadProgram64); the dispatcher
            // skips the RAX write for -K_CONTINUE so we don't clobber it.
            ret = sys_execve64(cpu, a1, a2, a3);
            break;
        case X64_SYS_getitimer:
        case X64_SYS_setitimer:
            ret = unimplementedSyscall64(nr);
            break;
        case X64_SYS_clone3:
            // glibc 2.34+ prefers clone3. Implement it directly (its ENOSYS
            // fallback to clone is fragile in our interpreter).
            if (cpu->thread && cpu->thread->process && cpu->thread->process->memory64) {
                ret = cpu->thread->process->clone364(cpu->thread, a1, a2);
            } else {
                ret = unimplementedSyscall64(nr);
            }
            break;
        case X64_SYS_eventfd2:
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            if (a2 & ~(K_O_CLOEXEC | K_O_NONBLOCK)) { ret = (U64)-K_EINVAL; break; }
            ret = (U64)(S64)(S32)syscall_eventfd2(cpu->thread, (U32)a1, (U32)a2);
            break;
        case X64_SYS_epoll_create:
        case X64_SYS_epoll_create1:
            // epoll_create(size) / epoll_create1(flags) — allocate an epoll set.
            // No guest memory. wineserver's main loop is epoll-driven. The
            // process epoll machinery is reused as-is.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
            ret = (U64)(S64)(S32)cpu->thread->process->epollcreate(
                (nr == X64_SYS_epoll_create) ? (U32)a1 : 0,
                (nr == X64_SYS_epoll_create1) ? (U32)a1 : 0);
            break;
        case X64_SYS_epoll_ctl: {
            // epoll_ctl(epfd, op, fd, event*). The epoll_event (12 bytes, packed
            // on x86-64: u32 events; u64 data) is layout-identical 32/64 but the
            // object reads it via thread->memory — bounce the 12 bytes through
            // 32-bit scratch. EPOLL_CTL_DEL passes a NULL event. No process
            // (bare selftest) → -EBADF, matching the kernel + the self-test.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_EBADF; break; }
            U32 ev32 = 0;
            if (a4) ev32 = bounceSockaddrTo32(cpu, a4, 12, nullptr);
            ret = (U64)(S64)(S32)cpu->thread->process->epollctl((FD)a1, (U32)a2, (FD)a3, ev32);
            if (getenv("BW64_IPCDUMP")) {
                U32 events = a4 ? cpu->thread->memory->readd(ev32 + 0) : 0;
                klog_fmt("IPC epoll_ctl(epfd=%d, op=%d, fd=%d, events=0x%x) -> %d",
                         (int)a1, (int)a2, (int)a3, events, (int)(S32)ret);
            }
            break;
        }
        case X64_SYS_epoll_wait: {
            // epoll_wait(epfd, events, maxevents, timeout). Bounce: let the
            // object write the result events into 32-bit scratch, then copy them
            // back to memory64. Cap maxevents so the scratch page holds them.
            // No process (bare selftest) → -EBADF.
            if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_EBADF; break; }
            U32 maxevents = (U32)a3;
            if (maxevents > 64) maxevents = 64;       // 64*12=768 < page
            U32 ev32 = bounceSockaddrTo32(cpu, 0, maxevents * 12, nullptr);
            if (!ev32) { ret = (U64)-K_EFAULT; break; }
            U32 rc = cpu->thread->process->epollwait(cpu->thread, (FD)a1, ev32, maxevents, (U32)a4);
            if ((S32)rc > 0) {
                U32 bytes = rc * 12;
                U8 tmp[64 * 12];
                cpu->thread->memory->memcpy(tmp, ev32, bytes);
                cpu->memory->memcpyToGuest(a2, tmp, bytes);
            }
            if (getenv("BW64_IPCDUMP") && (S32)rc > 0) {
                // Only log non-timeout waits — a returned event means a fd in the
                // set (wineserver socket or wait pipe) became readable.
                char fds[256] = {0}; int off = 0;
                for (U32 i = 0; i < rc && off < 230; i++) {
                    U32 ev   = cpu->thread->memory->readd(ev32 + i*12 + 0);
                    U64 data = cpu->thread->memory->readq(ev32 + i*12 + 4);
                    off += snprintf(fds + off, sizeof(fds) - off,
                                    "{ev=0x%x,data=0x%llx} ", ev,
                                    (unsigned long long)data);
                }
                klog_fmt("IPC epoll_wait(epfd=%d,to=%d) -> %d  %s",
                         (int)a1, (int)(S32)a4, (int)rc, fds);
            }
            // BW64_EPSPIN: identify a perpetually-ready fd (the Mode 2 spinner).
            // For each returned event, the epoll `data` is the guest fd number;
            // log that fd's kobject type so we can see WHAT keeps firing EPOLLIN.
            if (getenv("BW64_EPSPIN") && (S32)rc > 0) {
                for (U32 i = 0; i < rc; i++) {
                    U32 ev   = cpu->thread->memory->readd(ev32 + i*12 + 0);
                    U64 data = cpu->thread->memory->readq(ev32 + i*12 + 4);
                    KFileDescriptorPtr rfd = cpu->thread->process->getFileDescriptor((FD)data);
                    const char* kind = "none";
                    long recvUsed = -1, msgsN = -1; int inClosed = -1;
                    long pendTot = -1, pendLive = -1;
                    if (rfd && rfd->kobject) {
                        switch (rfd->kobject->type) {
                            case KTYPE_FILE: kind="file"; break;
                            case KTYPE_UNIX_SOCKET: {
                                kind="unixsock";
                                std::shared_ptr<KUnixSocketObject> us =
                                    std::dynamic_pointer_cast<KUnixSocketObject>(rfd->kobject);
                                if (us) { recvUsed=(long)us->debugRecvUsed(); msgsN=(long)us->debugMsgsSize(); inClosed=us->debugInClosed()?1:0; pendTot=(long)us->debugPendingTotal(); pendLive=(long)us->debugPendingLive(); }
                                break;
                            }
                            case KTYPE_NATIVE_SOCKET: kind="natsock"; break;
                            case KTYPE_EPOLL: kind="epoll"; break;
                            case KTYPE_EVENT: kind="event"; break;
                            case KTYPE_TIMER: kind="timer"; break;
                            case KTYPE_SIGNAL: kind="signal"; break;
                            default: kind="other"; break;
                        }
                    }
                    klog_fmt("EPSPIN pid=%d epfd=%d to=%d readyfd=%llu ev=0x%x kind=%s recvUsed=%ld msgs=%ld inClosed=%d pendTot=%ld pendLive=%ld",
                             (int)cpu->thread->process->id, (int)a1, (int)(S32)a4,
                             (unsigned long long)data, ev, kind, recvUsed, msgsN, inClosed, pendTot, pendLive);
                }
            }
            ret = (U64)(S64)(S32)rc;
            break;
        }
        case X64_SYS_getdents64:
            ret = sys_getdents64_real(cpu, a1, a2, a3);
            break;
        case X64_SYS_pwrite64:
            // pwrite64(fd, buf, count, offset) — wineserver populates registry/
            // config files in the prefix with it.
            if (a2 == 0) { ret = (U64)-K_EFAULT; break; }
            ret = sys_pwrite64(cpu, a1, a2, a3, a4);
            break;
        case X64_SYS_readv:
            // Scatter-gather read — walk the iovec, read into each segment.
            if (a2 == 0) { ret = (U64)-K_EFAULT; break; }
            ret = sys_readv64(cpu, a1, a2, a3);
            break;
        case X64_SYS_select:
            ret = unimplementedSyscall64(nr);
            break;
        case X64_SYS_chmod:
        case X64_SYS_fchmod:
            // No-op success: rootfs is effectively read-only for our purpose
            // and glibc's installer paths frequently call chmod on temp
            // files. Returning 0 avoids spurious install-time failures
            // without actually touching anything.
            ret = 0;
            break;
        case X64_SYS_gettimeofday: {
            // struct timeval { U64 sec; U64 usec; }
            if (a1) {
                U64 us = KSystem::getSystemTimeAsMicroSeconds();
                cpu->memory->writeq(a1,     us / 1000000ULL);
                cpu->memory->writeq(a1 + 8, us % 1000000ULL);
            }
            if (a2) {
                // struct timezone — minutes_west, dsttime. Zero both.
                cpu->memory->writed(a2,     0);
                cpu->memory->writed(a2 + 4, 0);
            }
            ret = 0;
            break;
        }
        case X64_SYS_getrusage:
            // struct rusage is large; for v1 just zero-fill 144 bytes.
            // glibc only inspects ru_utime and ru_stime on the startup path.
            if (a2) cpu->memory->memsetGuest(a2, 0, 144);
            ret = 0;
            break;
        case X64_SYS_times: {
            // times(&tms): struct tms is 4x clock_t = 32 bytes on x86_64.
            // Zero the accounting fields; return USER_HZ (100) ticks since an
            // arbitrary point so Mono/Unity startup timing moves forward.
            if (a1) cpu->memory->memsetGuest(a1, 0, 32);
            ret = KSystem::getSystemTimeAsMicroSeconds() / 10000ULL;
            break;
        }
        case X64_SYS_sysinfo:
            // struct sysinfo, 64-bit layout (112 bytes). The JVM and other
            // runtimes size their heaps from totalram, so it must be nonzero.
            if (a1) {
                auto* memory = cpu->memory;
                memory->memsetGuest(a1, 0, 112);
                memory->writeq(a1, KSystem::getMilliesSinceStart() / 1000);      // uptime
                memory->writeq(a1 + 32, 1024ULL * 1024 * 1024);                  // totalram
                memory->writeq(a1 + 40, 768ULL * 1024 * 1024);                   // freeram
                memory->writew(a1 + 80, 1);                                      // procs
                memory->writed(a1 + 104, 1);                                     // mem_unit
            }
            ret = 0;
            break;
        case X64_SYS_getppid:
            ret = cpu->thread && cpu->thread->process ? cpu->thread->process->parentId : 1;
            break;
        case X64_SYS_getpgrp:
        case X64_SYS_getpgid:
        case X64_SYS_getsid:
            ret = cpu->thread ? cpu->thread->id : 1;
            break;
        case X64_SYS_clock_getres:
            // 1 ns resolution claim. Some libc time helpers use this to size
            // a struct timespec field.
            if (a2) {
                cpu->memory->writeq(a2,     0);
                cpu->memory->writeq(a2 + 8, 1);
            }
            ret = 0;
            break;
        case X64_SYS_nanosleep:
            ret = sys_nanosleep64(cpu, a1, false, 0, 0);
            break;
        case X64_SYS_clock_nanosleep:
            ret = sys_nanosleep64(cpu, a3, true, a1, a2);
            break;
        case X64_SYS_membarrier: {
            // Linux 6.x command bits. The single-process virtual CPU makes
            // the expedited operation a sequentially consistent host fence.
            constexpr U32 QUERY = 0;
            constexpr U32 GLOBAL = 1;
            constexpr U32 GLOBAL_EXPEDITED = 2;
            constexpr U32 REGISTER_GLOBAL_EXPEDITED = 4;
            constexpr U32 PRIVATE_EXPEDITED = 8;
            constexpr U32 REGISTER_PRIVATE_EXPEDITED = 16;
            constexpr U32 PRIVATE_EXPEDITED_SYNC_CORE = 32;
            constexpr U32 REGISTER_PRIVATE_EXPEDITED_SYNC_CORE = 64;
            constexpr U32 PRIVATE_EXPEDITED_RSEQ = 128;
            constexpr U32 REGISTER_PRIVATE_EXPEDITED_RSEQ = 256;
            constexpr U32 SUPPORTED = QUERY | GLOBAL | GLOBAL_EXPEDITED |
                REGISTER_GLOBAL_EXPEDITED | PRIVATE_EXPEDITED |
                REGISTER_PRIVATE_EXPEDITED | PRIVATE_EXPEDITED_SYNC_CORE |
                REGISTER_PRIVATE_EXPEDITED_SYNC_CORE | PRIVATE_EXPEDITED_RSEQ |
                REGISTER_PRIVATE_EXPEDITED_RSEQ;
            const U32 command = (U32)a1;
            const U32 flags = (U32)a2;
            if (flags != 0 || a3 != 0) { ret = (U64)-K_EINVAL; break; }
            if (command == QUERY) { ret = SUPPORTED; break; }
            if (command & ~SUPPORTED) { ret = (U64)-K_EINVAL; break; }
            if (command == REGISTER_GLOBAL_EXPEDITED) { ret = 0; break; }
            if (command == REGISTER_PRIVATE_EXPEDITED ||
                command == REGISTER_PRIVATE_EXPEDITED_SYNC_CORE ||
                command == REGISTER_PRIVATE_EXPEDITED_RSEQ) {
                if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
                cpu->thread->process->membarrierRegistrations |= command;
                ret = 0;
                break;
            }
            if (command == PRIVATE_EXPEDITED ||
                command == PRIVATE_EXPEDITED_SYNC_CORE ||
                command == PRIVATE_EXPEDITED_RSEQ) {
                if (!cpu->thread || !cpu->thread->process) { ret = (U64)-K_ENOSYS; break; }
                const U32 registration = command == PRIVATE_EXPEDITED ? REGISTER_PRIVATE_EXPEDITED :
                    command == PRIVATE_EXPEDITED_SYNC_CORE ? REGISTER_PRIVATE_EXPEDITED_SYNC_CORE :
                    REGISTER_PRIVATE_EXPEDITED_RSEQ;
                if (!(cpu->thread->process->membarrierRegistrations & registration)) {
                    ret = (U64)-K_EPERM;
                    break;
                }
            }
            std::atomic_thread_fence(std::memory_order_seq_cst);
            ret = 0;
            break;
        }
        case X64_SYS_rseq:
            // No rseq implementation: report through the common ENOSYS path.
            ret = unimplementedSyscall64(nr);
            break;
        case X64_SYS_ptrace:
            ret = (U64)-K_EPERM;
            break;
        case X64_SYS_exit:
            ret = sys_exit64(cpu, a1, false);
            break;
        case X64_SYS_exit_group:
            ret = sys_exit64(cpu, a1, true);
            break;
        case 74:  // fsync(fd)
        case 75:  // fdatasync(fd)
        case 26:  // msync(addr,len,flags)
        case X64_SYS_sync_file_range:
            // Our VFS writes are effectively synchronous (no host page cache to
            // flush for the guest's purposes), so a flush is a successful no-op.
            // Previously these fell to the default -ENOSYS, which some callers
            // (e.g. fontconfig cache write, sqlite, wine's registry save) RETRY
            // — appearing as a repeating "unimplemented syscall #74" near a hang.
            ret = 0;
            break;
        default:
            ret = unimplementedSyscall64(nr);
            break;
    }

    // execve (and any future blocking/restart paths) return -K_CONTINUE to mean
    // "the new image / restart already set the registers — do NOT overwrite RAX
    // with this sentinel." Mirrors the 32-bit dispatcher (syscall.cpp).
    if (ret == (U64)(S64)-K_CONTINUE) {
        if (bw64MetricsOn && !bw64BridgeTrap)
            bw64_metrics_add_stage(BW64_STAGE_SYSCALL, bw64_metrics_now_ms() - bw64T0, 0);
        return;
    }
    if (bw64MetricsOn && !bw64BridgeTrap)
        bw64_metrics_add_stage(BW64_STAGE_SYSCALL, bw64_metrics_now_ms() - bw64T0, 0);
    if (ret == (U64)(S64)-K_WAIT) {
        return;
    }
    cpu->reg[X64_RAX].setU64(ret);
}

#endif // BOXEDWINE_GUEST_X64
