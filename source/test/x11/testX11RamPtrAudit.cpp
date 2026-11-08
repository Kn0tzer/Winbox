/*
 * Audit tests for the X11 bridge's use of KMemory::getRamPtr.
 *
 * getRamPtr returns nullptr when a guest address is unmapped, not
 * writable, or its span crosses a page boundary. The handlers under
 * test received guest pointers and dereferenced the result unchecked,
 * so a guest passing a page-straddling struct crashed the host. These
 * tests drive the handlers (through the real int 0x99 callback table)
 * and XSetWindowAttributes::get with page-straddling or read-only
 * guest structs.
 *
 * Handler invocations run in a forked child: on the unfixed code the
 * straddling cases dereference the null host pointer — the child
 * dies by signal or wedges in the host fault handler — and the
 * parent reports a test failure instead of taking the suite down.
 * On the fixed code every child exits cleanly, because the handlers
 * read guest structs through the checked accessors.
 */
#include "boxedwine.h"

#ifdef __TEST

#include "../cpu/testCPU.h"
#include "testX11RamPtrAudit.h"
#include "../../../tools/x11/X11_def.h"
#include "../../x11/x11.h"
#include "../../x11/xwindow.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

// Populated by x11_init() in x11common.cpp; neither symbol is declared
// in a header the test can include.
void x11_init();
extern Int99Callback int9BCallback[];

static void ensureX11Init() {
    static bool initialized = false;
    if (!initialized) {
        x11_init();
        initialized = true;
    }
}

// A two-page scratch mapping for page-straddling guest structs.
// Handler arguments are written to the effective address CPU::peek32
// reads: the SS segment base plus ESP + n*4, on the test process's
// own stack mapping (the slots above ESP are where int 0x99
// arguments sit).
struct X11AuditScratch {
    U32 base = 0;       // two page-straddling pages

    bool setup(KMemory* memory, KThread* thread) {
        base = memory->mmap(thread, 0, 2 * K_PAGE_SIZE, K_PROT_READ | K_PROT_WRITE, K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
        if ((S32)base < 0) return false;
        memory->memset(base, 0, 2 * K_PAGE_SIZE);
        return true;
    }
    void setArg(KMemory* memory, CPU* cpu, U32 index, U32 value) {
        U32 eff = cpu->seg[SS].address + ((cpu->reg[4].u32 + index * 4) & cpu->stackMask);
        memory->writed(eff, value);
    }
    void teardown(KMemory* memory) {
        if (base) memory->unmap(base, 2 * K_PAGE_SIZE);
    }
};

// Runs one handler invocation in a forked child and returns the
// handler's EAX through a pipe. Returns false (with a test failure
// recorded) if the child dies, wedges past the budget, or exits
// without reporting.
static bool runHandlerIsolated(Int99Callback handler, CPU* cpu, const char* caseName, U32& eaxOut) {
    int fds[2];
    if (pipe(fds) != 0) {
        testFail("%s: pipe failed", caseName);
        return false;
    }
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        handler(cpu);
        U32 eax = cpu->reg[0].u32;
        ssize_t ignored = write(fds[1], &eax, sizeof(eax));
        (void)ignored;
        _exit(0);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        testFail("%s: fork failed", caseName);
        return false;
    }
    // ~10 s budget in 50 ms polls; a healthy call is microseconds.
    int status = 0;
    bool exited = false;
    for (int i = 0; i < 200; i++) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) { exited = true; break; }
        usleep(50000);
    }
    if (!exited) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        close(fds[0]);
        testFail("%s: handler did not finish (child killed after timeout)", caseName);
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        close(fds[0]);
        testFail("%s: handler child died (status %d)", caseName, status);
        return false;
    }
    U32 eax = 0;
    ssize_t got = read(fds[0], &eax, sizeof(eax));
    close(fds[0]);
    if (got != (ssize_t)sizeof(eax)) {
        testFail("%s: handler child reported no result", caseName);
        return false;
    }
    eaxOut = eax;
    return true;
}

void testX11RamPtrGetFallback() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;
    KThread* thread = context.thread;

    // Backed, writable page: get() must keep returning the direct
    // pointer with the guest's values visible through it.
    U32 base = memory->mmap(thread, 0, K_PAGE_SIZE, K_PROT_READ | K_PROT_WRITE, K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    if ((S32)base < 0) { testFail("RW mmap failed: %08X", base); return; }
    memory->memset(base, 0, K_PAGE_SIZE);
    U32 address = base + 100;
    memory->writed(address + offsetof(XSetWindowAttributes, event_mask), 0x00C0FFEE);
    XSetWindowAttributes tmp;
    XSetWindowAttributes* result = XSetWindowAttributes::get(memory, address, &tmp);
    if (!result) {
        testFail("get() returned null for a backed writable page");
    } else if (result == &tmp) {
        testFail("get() used the fallback for a backed writable page");
    } else if (result->event_mask != (S32)0x00C0FFEE) {
        testFail("get() direct pointer lost the guest value: %08X", (U32)result->event_mask);
    }
    memory->unmap(base, K_PAGE_SIZE);

    // Read-only page: getRamPtr(write=true) refuses with nullptr. The
    // unfixed get() returns that null to its callers; the fixed get()
    // falls back to the checked read path and returns tmp.
    U32 roBase = memory->mmap(thread, 0, K_PAGE_SIZE, K_PROT_READ, K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    if ((S32)roBase < 0) { testFail("RO mmap failed: %08X", roBase); return; }
    XSetWindowAttributes tmp2;
    XSetWindowAttributes* roResult = XSetWindowAttributes::get(memory, roBase + 100, &tmp2);
    if (roResult != &tmp2) {
        testFail("get() did not fall back to the checked read for a read-only page (returned %p)", (void*)roResult);
    } else if (tmp2.event_mask != 0) {
        testFail("get() fallback read a nonzero event_mask from a fresh page: %08X", (U32)tmp2.event_mask);
    }
    memory->unmap(roBase, K_PAGE_SIZE);
}

void testX11RamPtrFreeHandlers() {
    ensureX11Init();
    TestContext& context = testContext();
    KMemory* memory = context.memory;
    KThread* thread = context.thread;
    CPU* cpu = context.cpu;
    X11AuditScratch scratch;
    if (!scratch.setup(memory, thread)) { testFail("scratch mmap failed"); return; }
    U32 pageEnd = scratch.base + K_PAGE_SIZE;
    U32 eax = 0;

    // Control: a within-page XRRCrtcInfo with zeroed fields completes
    // on both the unfixed and fixed code.
    scratch.setArg(memory, cpu, 1, scratch.base + 2048);
    runHandlerIsolated(int9BCallback[X11_XRR_FREE_CRTC_INFO], cpu, "XRRFreeCrtcInfo within-page control", eax);

    // XRRFreeCrtcInfo with the struct straddling the page boundary:
    // getRamPtr refuses the span, and the unfixed handler dereferences
    // the null. The fixed handler reads the (zero) fields through the
    // checked accessor and completes.
    scratch.setArg(memory, cpu, 1, pageEnd - 8);
    runHandlerIsolated(int9BCallback[X11_XRR_FREE_CRTC_INFO], cpu, "XRRFreeCrtcInfo straddling struct", eax);

    // XFreeModifierMap with its 8-byte struct straddling the boundary
    // (the modifiermap field itself is the first word of page two).
    // The unfixed handler also lacked any null-argument guard.
    scratch.setArg(memory, cpu, 1, pageEnd - 4);
    if (runHandlerIsolated(int9BCallback[X11_FREE_MODIFIER_MAP], cpu, "XFreeModifierMap straddling struct", eax)) {
        if (eax != 0) {
            testFail("XFreeModifierMap straddle did not return Success: EAX=%08X", eax);
        }
    }

    scratch.teardown(memory);
}

void testX11RamPtrGetCrtcInfoStraddle() {
    ensureX11Init();
    TestContext& context = testContext();
    KMemory* memory = context.memory;
    KThread* thread = context.thread;
    CPU* cpu = context.cpu;
    X11AuditScratch scratch;
    if (!scratch.setup(memory, thread)) { testFail("scratch mmap failed"); return; }
    U32 pageEnd = scratch.base + K_PAGE_SIZE;
    U32 eax = 0;

    // Control: within-page resources struct with ncrtc == 0 returns 0
    // on both the unfixed and fixed code.
    scratch.setArg(memory, cpu, 2, scratch.base + 2048);
    scratch.setArg(memory, cpu, 3, 1);
    if (runHandlerIsolated(int9BCallback[X11_XRR_GET_CRTC_INFO], cpu, "XRRGetCrtcInfo within-page control", eax)) {
        if (eax != 0) {
            testFail("XRRGetCrtcInfo control did not return 0: EAX=%08X", eax);
        }
    }

    // Straddling resources struct: getRamPtr refuses the span and the
    // unfixed handler dereferences the null reading ncrtc. The fixed
    // handler reads ncrtc == 0 through the checked accessor.
    scratch.setArg(memory, cpu, 2, pageEnd - 16);
    scratch.setArg(memory, cpu, 3, 1);
    if (runHandlerIsolated(int9BCallback[X11_XRR_GET_CRTC_INFO], cpu, "XRRGetCrtcInfo straddling struct", eax)) {
        if (eax != 0) {
            testFail("XRRGetCrtcInfo straddle did not return 0: EAX=%08X", eax);
        }
    }

    scratch.teardown(memory);
}

void testX11RamPtrSetCrtcConfigStraddle() {
    ensureX11Init();
    TestContext& context = testContext();
    KMemory* memory = context.memory;
    KThread* thread = context.thread;
    CPU* cpu = context.cpu;
    X11AuditScratch scratch;
    if (!scratch.setup(memory, thread)) { testFail("scratch mmap failed"); return; }
    U32 pageEnd = scratch.base + K_PAGE_SIZE;
    U32 eax = 0;

    // Arguments that pass the handler's first guard: crtc 1, x/y 0,
    // rotation RR_Rotate_0 (1), one output whose id cell reads 1, and
    // mode id 1. The zeroed resources struct then has nmode == 0, so
    // the fixed handler refuses with RRSetConfigFailed (3). On the
    // unfixed code the same call dereferences the null getRamPtr
    // result for the straddling struct.
    U32 outputsCell = scratch.base + 3072;
    memory->writed(outputsCell, 1);
    scratch.setArg(memory, cpu, 2, pageEnd - 16);
    scratch.setArg(memory, cpu, 3, 1);
    scratch.setArg(memory, cpu, 5, 0);
    scratch.setArg(memory, cpu, 6, 0);
    scratch.setArg(memory, cpu, 7, 1);
    scratch.setArg(memory, cpu, 8, 1); // RR_Rotate_0
    scratch.setArg(memory, cpu, 9, outputsCell);
    scratch.setArg(memory, cpu, 10, 1);
    if (runHandlerIsolated(int9BCallback[X11_XRR_SET_CRTC_CONFIG], cpu, "XRRSetCrtcConfig straddling struct", eax)) {
        if (eax != 3) {
            testFail("XRRSetCrtcConfig straddle did not return RRSetConfigFailed: EAX=%08X", eax);
        }
    }

    scratch.teardown(memory);
}

#endif
