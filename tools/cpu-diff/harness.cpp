// cpu-diff harness: differential tester for the Boxedwine64 CPU64 emulator.
// For each test case: run the snippet natively (forked child) and in the
// CPU64 interpreter, diff registers/flags/scratch memory, measure timing.
//
// Build: ./build.sh   Run: ./run.sh   (see METHOD.md)
// Test-only scaffolding: a few unreachable-in-tests symbols are stubbed below
// (ksyscall64, raiseSyncFault, ...). No emulator behavior is modified.

#include "boxedwine.h"
#ifdef BOXEDWINE_GUEST_X64
#include "cpu64.h"
#include "kmemory64.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <string>
#include <map>
#include <limits>
#include <chrono>
#include <functional>
#include <algorithm>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#ifdef BOXEDWINE_GUEST_X64

// ---------------------------------------------------------------------------
// Test-only stubs for symbols the differential snippets can never reach
// (no syscalls, no signals, no faults expected). If a snippet DOES fault,
// raiseSyncFault throws DiffFault so the case is reported instead of hanging.
// ---------------------------------------------------------------------------
struct DiffFault : std::runtime_error {
    explicit DiffFault(const std::string& s) : std::runtime_error(s) {}
};

void ksyscall64(CPU64* /*cpu*/) { throw DiffFault("unexpected syscall"); }
U64 KSystem::getSystemTimeAsMicroSeconds() { return 0; }
void KProcess::signalProcess(U32 /*signal*/) { throw DiffFault("unexpected signalProcess"); }
thread_local KThread* KThread::runningThread = nullptr;
bool CPU64::raiseSyncFault(U32 /*sig*/, U32 /*trapNo*/, S32 /*siCode*/, U64 faultAddr) {
    throw DiffFault("sync fault at guest addr");
}
bool CPU64::deliverPendingSignals() { return false; }

// klog -> stderr so unimplemented-opcode messages stay visible in the log.
// (The routine INT3-terminator message is filtered as expected noise.)
void klog(const char* msg) {
    if (strstr(msg, "INT3 at RIP=") == nullptr)
        fprintf(stderr, "[emulator klog] %s\n", msg);
}
void klog_nonewline(const char* msg) { fprintf(stderr, "[emulator klog] %s", msg); }
void internal_log(BString msg, FILE* f) {
    const char* s = msg.c_str();
    if (strstr(s, "INT3 at RIP=") != nullptr) return; // expected terminator noise
    fprintf(f ? f : stderr, "[emulator] %s\n", s);
}
void kpanic(const char* msg) {
    fprintf(stderr, "[emulator kpanic] %s\n", msg);
    abort();
}
bool KSystem::useF64 = false; // matches ksystem.cpp for BOXEDWINE_GUEST_X64

// 32-bit KMemory methods referenced by fpu.cpp's 32-bit EA helpers.
// Unreachable from 64-bit code paths (those use KMemory64); stubbed to fail
// loudly if ever hit.
void KMemory::memcpy(U32, const void*, U32) { throw DiffFault("32-bit KMemory::memcpy"); }
U64 KMemory::readq(U32) { throw DiffFault("32-bit KMemory::readq"); }
U32 KMemory::readd(U32) { throw DiffFault("32-bit KMemory::readd"); }
U16 KMemory::readw(U32) { throw DiffFault("32-bit KMemory::readw"); }
void KMemory::writed(U32, U32) { throw DiffFault("32-bit KMemory::writed"); }
void KMemory::writeq(U32, U64) { throw DiffFault("32-bit KMemory::writeq"); }
void KMemory::writew(U32, U16) { throw DiffFault("32-bit KMemory::writew"); }

// ---------------------------------------------------------------------------
// Guest layout notes.
// The box is ARM64, so native x86-64 executes under qemu-x86_64 (see
// METHOD.md). Numeric stack/scratch addresses therefore differ between the
// two sides; the differ compares register *values* (inputs are identical)
// and scratch *contents*, with rsp checked per-side for balance. Only the
// emulator uses these fixed addresses.
// ---------------------------------------------------------------------------
static constexpr U64 CODE_BASE   = 0x400000ULL; // emulator: code page
static constexpr U64 STACK_PAGE  = 0x7FF000ULL; // emulator: stack page
static constexpr U64 RSP_SNIPPET = 0x7FFFF0ULL; // emulator: rsp during snippet
static constexpr U64 MEM_SCRATCH = 0x900000ULL; // emulator: scratch page (16B aligned)
static constexpr U64 RSP_INIT    = 0x7FFFF8ULL; // initial regs[X64_RSP] (emulator)

// ---------------------------------------------------------------------------
// Native runner: x86-64 snippets execute under qemu-x86_64 via
// runner/runner.elf (static ELF, built with x86_64-linux-gnu-g++).
// Batch protocol: the harness writes N cases to a temp input file, spawns
// `qemu-x86_64 runner.elf in out`, and reads N results. The runner forks
// one child per case for crash isolation.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct QemuCase {
    U64 codeLen = 0;
    U8 code[4096] = {};
    U64 regsIn[16] = {};
    U32 flagsIn = 0;
    U32 patchRbx = 0; // runner sets regsIn[3] = scratch address
    U8 scratchIn[256] = {};
};
struct QemuResult {
    U32 ok = 0, crashed = 0, timeout = 0, pad = 0;
    U64 regs[16] = {};
    U32 flags = 0, pad2 = 0;
    U64 rspIn = 0, rbxIn = 0;
    U8 scratch[256] = {};
    U64 elapsedNs = 0;
};
#pragma pack(pop)

struct NativeResult {
    bool ok = false;
    bool crashed = false;
    bool timeout = false;
    U64 regs[16] = {};
    U32 flags = 0;
    U64 rspIn = 0, rbxIn = 0;
    U8 scratch[256] = {};
};

static std::string g_runnerPath; // set in main()
static std::string g_qemuPath = "/usr/bin/qemu-x86_64-static";

struct TestCase {
    std::string set;          // "int" | "fpu" | "sse"
    std::string name;         // "add"
    std::string form;         // "r64,r64"
    std::vector<U8> code;     // snippet bytes (terminator appended by runners)
    U64 r[16] = {};           // initial regs (X64Reg order); r[X64_RSP] auto
    U32 flagsIn = 0x202;
    U32 flagsMask = 0;        // defined flags to compare
    bool checkMem = false;    // compare MEM_SCRATCH bytes
    int memLen = 0;
    U8 memInit[256] = {};     // initial scratch content
    bool checkCfPreserved = false;   // inc/dec: CF must be unchanged
    bool checkFlagsUnchanged = false;// e.g. shift by masked-count 0: no flag change
};

static bool writeAll(int fd, const void* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = write(fd, (const char*)buf + off, n - off);
        if (r <= 0) return false;
        off += (size_t)r;
    }
    return true;
}
static bool readAll(int fd, void* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, (char*)buf + off, n - off);
        if (r <= 0) return false;
        off += (size_t)r;
    }
    return true;
}

// Run a batch of cases natively.
static std::vector<NativeResult> runNativeBatch(
    const std::vector<TestCase>& cases, const std::vector<int>& idxs) {
    std::vector<NativeResult> out(idxs.size());
    if (idxs.empty()) return out;
    char inTmp[] = "/tmp/cpudiff-in-XXXXXX";
    char outTmp[] = "/tmp/cpudiff-out-XXXXXX";
    int fdIn = mkstemp(inTmp);
    int fdOut = mkstemp(outTmp);
    if (fdIn < 0 || fdOut < 0) return out;
    U32 n = (U32)idxs.size();
    writeAll(fdIn, &n, 4);
    for (size_t k = 0; k < idxs.size(); k++) {
        const TestCase& tc = cases[idxs[k]];
        QemuCase c;
        memset(&c, 0, sizeof(c));
        c.codeLen = tc.code.size() > sizeof(c.code) ? sizeof(c.code) : tc.code.size();
        memcpy(c.code, tc.code.data(), c.codeLen);
        memcpy(c.regsIn, tc.r, sizeof(c.regsIn));
        c.flagsIn = tc.flagsIn;
        c.patchRbx = (tc.checkMem || tc.set == "fpu" || tc.set == "sse") ? 1 : 0;
        if (tc.checkMem) memcpy(c.scratchIn, tc.memInit, sizeof(c.scratchIn));
        writeAll(fdIn, &c, sizeof(c));
    }
    close(fdIn);
    close(fdOut);

    pid_t pid = fork();
    if (pid == 0) {
        execl(g_qemuPath.c_str(), "qemu-x86_64", g_runnerPath.c_str(), inTmp, outTmp,
              (char*)nullptr);
        _exit(127);
    }
    int wstatus = 0, waited = 0;
    bool qemuOk = false;
    while (waited < 120000) {
        pid_t w = waitpid(pid, &wstatus, WNOHANG);
        if (w == pid) { qemuOk = WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0; break; }
        if (w < 0) break;
        usleep(2000);
        waited += 2;
    }
    if (waited >= 120000) { kill(pid, SIGKILL); waitpid(pid, &wstatus, 0); }
    unlink(inTmp);
    if (qemuOk) {
        int fd = open(outTmp, O_RDONLY);
        if (fd >= 0) {
            U32 rn = 0;
            if (readAll(fd, &rn, 4) && rn == n) {
                for (size_t k = 0; k < idxs.size(); k++) {
                    QemuResult q;
                    memset(&q, 0, sizeof(q));
                    if (!readAll(fd, &q, sizeof(q))) break;
                    NativeResult& r = out[k];
                    if (q.ok) {
                        r.ok = true;
                        memcpy(r.regs, q.regs, sizeof(r.regs));
                        r.flags = q.flags;
                        r.rspIn = q.rspIn; r.rbxIn = q.rbxIn;
                        memcpy(r.scratch, q.scratch, sizeof(r.scratch));
                    } else if (q.timeout) {
                        r.timeout = true;
                    } else {
                        r.crashed = true;
                    }
                }
            }
            close(fd);
        }
    }
    unlink(outTmp);
    return out;
}


// ---------------------------------------------------------------------------
// Emulator runner: drive CPU64 directly (mirrors cpu64SelfTest.cpp).
// Snippet is terminated with 0xCC (INT3 -> yield, registers preserved).
// ---------------------------------------------------------------------------
struct EmuResult {
    bool ok = false;
    bool faulted = false;      // DiffFault (sync fault / syscall / signal)
    std::string err;
    U64 regs[16] = {};
    U32 flags = 0;
    U8 scratch[256] = {};
    U64 elapsedNs = 0;
    bool yieldAtEnd = false;
};

static EmuResult runEmu(const U8* code, size_t len, const U64 regsIn[16],
                        U32 flagsIn, const U8* scratchIn, U64 insnLimit = 100000) {
    EmuResult r;
    try {
        KMemory64 mem(nullptr);
        auto mapOk = [](U64 got, U64 want) {
            return got == want; // (U64)-errno on failure
        };
        if (!mapOk(mem.mmapAnonymousFixed(CODE_BASE, 0x1000, 7), CODE_BASE) ||
            !mapOk(mem.mmapAnonymousFixed(STACK_PAGE, 0x1000, 3), STACK_PAGE) ||
            !mapOk(mem.mmapAnonymousFixed(MEM_SCRATCH, 0x1000, 3), MEM_SCRATCH)) {
            r.err = "mmapAnonymousFixed failed";
            return r;
        }
        std::vector<U8> withTerm(code, code + len);
        withTerm.push_back(0xCC); // INT3 terminator
        mem.memcpyToGuest(CODE_BASE, withTerm.data(), withTerm.size());
        if (scratchIn)
            mem.memcpyToGuest(MEM_SCRATCH, scratchIn, 256);

        CPU64 cpu(&mem);
        cpu.rip = CODE_BASE;
        for (int i = 0; i < 16; i++) cpu.reg[i].setU64(regsIn[i]);
        cpu.reg[X64_RSP].setU64(RSP_SNIPPET); // == native post-call rsp
        cpu.rflags = flagsIn;

        auto t0 = std::chrono::steady_clock::now();
        cpu.runBounded(insnLimit);
        auto t1 = std::chrono::steady_clock::now();
        r.elapsedNs =
            (U64)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

        U64 expectRip = CODE_BASE + len + 1;
        r.yieldAtEnd = cpu.yield && cpu.rip == expectRip;
        if (!r.yieldAtEnd) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "did not reach INT3 terminator (yield=%d rip=0x%llx, want 0x%llx)",
                     (int)cpu.yield, (unsigned long long)cpu.rip,
                     (unsigned long long)expectRip);
            r.err = buf;
            return r;
        }
        for (int i = 0; i < 16; i++) r.regs[i] = cpu.reg[i].u64;
        r.flags = cpu.rflags;
        mem.memcpyFromGuest(r.scratch, MEM_SCRATCH, sizeof(r.scratch));
        r.ok = true;
    } catch (const DiffFault& f) {
        r.faulted = true;
        r.err = f.what();
    } catch (const std::exception& e) {
        r.err = std::string("exception: ") + e.what();
    }
    return r;
}

// ---------------------------------------------------------------------------
// Test case definition + byte emitters (hand-assembled, deterministic).
// Register indices follow X64Reg (0=rax .. 15=r15), matching ModR/M encoding.
// ---------------------------------------------------------------------------

static void eRex(std::vector<U8>& b, bool w, int reg, int rm) {
    U8 r = (U8)(0x40 | (w ? 8 : 0) | ((reg & 8) ? 4 : 0) | ((rm & 8) ? 1 : 0));
    if (r != 0x40) b.push_back(r);
}
static void eModRM(std::vector<U8>& b, int mod, int reg, int rm) {
    b.push_back((U8)((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}
static void eU32(std::vector<U8>& b, U32 v) {
    for (int i = 0; i < 4; i++) b.push_back((U8)(v >> (i * 8)));
}
// ALU r/m64, r64  (opcodes 0x01,0x09,0x11,0x19,0x21,0x29,0x31,0x39): dst=rm
static void eRR(std::vector<U8>& b, U8 op, int dst, int src) {
    eRex(b, true, src, dst); b.push_back(op); eModRM(b, 3, src, dst);
}
// ALU r64, imm32  (0x81 /ext)
static void eRI32(std::vector<U8>& b, int ext, int dst, U32 imm) {
    eRex(b, true, 0, dst); b.push_back(0x81); eModRM(b, 3, ext, dst); eU32(b, imm);
}
// ALU r64, imm8  (0x83 /ext)
static void eRI8(std::vector<U8>& b, int ext, int dst, U8 imm) {
    eRex(b, true, 0, dst); b.push_back(0x83); eModRM(b, 3, ext, dst); b.push_back(imm);
}
// Group F7/FF: 0xF7 /ext r64 ; 0xFF /ext r64
static void eGrp(std::vector<U8>& b, U8 op, int ext, int reg) {
    eRex(b, true, 0, reg); b.push_back(op); eModRM(b, 3, ext, reg);
}
// shift r64,1 (0xD1) / r64,cl (0xD3) / r64,imm8 (0xC1), ext selects op
static void eShift(std::vector<U8>& b, U8 op, int ext, int reg) {
    eRex(b, true, 0, reg); b.push_back(op); eModRM(b, 3, ext, reg);
}
static void eShiftI(std::vector<U8>& b, int ext, int reg, U8 imm) {
    eRex(b, true, 0, reg); b.push_back(0xC1); eModRM(b, 3, ext, reg); b.push_back(imm);
}

static const U32 F_CF = X64_CF, F_PF = X64_PF, F_AF = X64_AF,
                 F_ZF = X64_ZF, F_SF = X64_SF, F_OF = X64_OF;
static const U32 F_ALL6 = X64_CF | X64_PF | X64_AF | X64_ZF | X64_SF | X64_OF;

static U64 guardReg(int i) { return 0xA55A0000A55A0000ULL ^ (U64)(U32)(i * 0x11111111); }

static void initRegs(TestCase& tc, U64 raxVal, U64 rbxVal) {
    for (int i = 0; i < 16; i++) tc.r[i] = guardReg(i);
    tc.r[X64_RAX] = raxVal;
    tc.r[X64_RBX] = rbxVal;
    tc.r[X64_RSP] = RSP_INIT;
}

// Curated (a=rax init, b=rbx init) edge pairs for 64-bit ALU.
static const U64 kPairs[][2] = {
    {0, 0}, {0, 1}, {1, 0}, {1, 1}, {5, 3},
    {0xFFFFFFFFFFFFFFFFULL, 1}, {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
    {0x8000000000000000ULL, 0x8000000000000000ULL},
    {0x8000000000000000ULL, 1}, {0x7FFFFFFFFFFFFFFFULL, 1},
    {0x123456789ABCDEF0ULL, 0x0FEDCBA987654321ULL},
    {0x8000000000000001ULL, 0x7FFFFFFFFFFFFFFFULL},
};
static const int kNumPairs = (int)(sizeof(kPairs) / sizeof(kPairs[0]));

// ---------------------------------------------------------------------------
// Integer ALU test builders.
// ---------------------------------------------------------------------------
static void addCase(std::vector<TestCase>& out, const char* set, const char* name,
                    const char* form, const std::vector<U8>& code,
                    U64 a, U64 b, U32 flagsMask, U32 flagsIn = 0x202) {
    TestCase tc;
    tc.set = set; tc.name = name; tc.form = form; tc.code = code;
    initRegs(tc, a, b);
    tc.flagsIn = flagsIn; tc.flagsMask = flagsMask;
    out.push_back(tc);
}

// Generic r64,r64 + r64,imm8 + r64,imm32 ALU (op, ext): add/sub/adc/sbb/...
static void aluForms(std::vector<TestCase>& out, const char* name, U8 opRR, int ext,
                     U32 flagsMask, bool varyCF) {
    U32 fis[] = {0x202, 0x203};
    int nfi = varyCF ? 2 : 1;
    for (int fi = 0; fi < nfi; fi++)
        for (int i = 0; i < kNumPairs; i++) {
            std::vector<U8> b; eRR(b, opRR, X64_RAX, X64_RBX);
            addCase(out, "int", name, "r64,r64", b, kPairs[i][0], kPairs[i][1],
                    flagsMask, fis[fi]);
        }
    for (int fi = 0; fi < nfi; fi++)
        for (int i = 0; i < kNumPairs; i++) {
            std::vector<U8> b; eRI8(b, ext, X64_RAX, (U8)kPairs[i][1]);
            addCase(out, "int", name, "r64,imm8", b, kPairs[i][0], kPairs[i][1],
                    flagsMask, fis[fi]);
        }
    for (int fi = 0; fi < nfi; fi++) {
        U32 imms[] = {0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x12345678};
        for (U32 imm : imms) {
            std::vector<U8> b; eRI32(b, ext, X64_RAX, imm);
            addCase(out, "int", name, "r64,imm32", b, kPairs[fi % kNumPairs][0], 0,
                    flagsMask, fis[fi]);
        }
    }
}

static void buildIntCases(std::vector<TestCase>& out) {
    // mov
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; eRR(b, 0x89, X64_RAX, X64_RBX); // mov rax, rbx
        addCase(out, "int", "mov", "r64,r64", b, kPairs[i][0], kPairs[i][1], 0);
    }
    {
        U64 imms[] = {0, 1, 0x7FFFFFFFFFFFFFFFULL, 0x8000000000000000ULL,
                      0xFFFFFFFFFFFFFFFFULL, 0x123456789ABCDEF0ULL};
        for (U64 imm : imms) { // mov rax, imm32 (sign-extended)
            std::vector<U8> b;
            eRex(b, true, 0, X64_RAX); b.push_back(0xC7); eModRM(b, 3, 0, X64_RAX);
            eU32(b, (U32)imm);
            addCase(out, "int", "mov", "r64,imm32", b, 0xAAAAAAAAAAAAAAAAULL, 0, 0);
        }
        for (U64 imm : imms) { // mov rax, imm64
            std::vector<U8> b;
            eRex(b, true, 0, X64_RAX); b.push_back((U8)(0xB8 + X64_RAX));
            for (int k = 0; k < 8; k++) b.push_back((U8)(imm >> (k * 8)));
            addCase(out, "int", "mov", "r64,imm64", b, 0xAAAAAAAAAAAAAAAAULL, 0, 0);
        }
    }
    // push / pop (balanced pairs)
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; b.push_back(0x50); b.push_back(0x5B); // push rax; pop rbx
        addCase(out, "int", "push", "r64+pop r64", b, kPairs[i][0], kPairs[i][1], 0);
    }
    {
        U32 imms[] = {0, 1, 0x80000000, 0xFFFFFFFF};
        for (U32 imm : imms) { // push imm32; pop rbx
            std::vector<U8> b; b.push_back(0x68); eU32(b, imm); b.push_back(0x5B);
            addCase(out, "int", "push", "imm32+pop r64", b, 0, 0, 0);
        }
        U8 imms8[] = {0, 1, 0x80, 0xFF};
        for (U8 imm : imms8) { // push imm8; pop rbx
            std::vector<U8> b; b.push_back(0x6A); b.push_back(imm); b.push_back(0x5B);
            addCase(out, "int", "push", "imm8+pop r64", b, 0, 0, 0);
        }
    }
    // arithmetic / logic
    aluForms(out, "add", 0x01, 0, F_ALL6, false);
    aluForms(out, "sub", 0x29, 5, F_ALL6, false);
    aluForms(out, "adc", 0x11, 2, F_ALL6, true);
    aluForms(out, "sbb", 0x19, 3, F_ALL6, true);
    aluForms(out, "and", 0x21, 4, F_CF | F_PF | F_ZF | F_SF | F_OF, false);
    aluForms(out, "or",  0x09, 1, F_CF | F_PF | F_ZF | F_SF | F_OF, false);
    aluForms(out, "xor", 0x31, 6, F_CF | F_PF | F_ZF | F_SF | F_OF, false);
    // cmp (flags only) / test (flags only)
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; eRR(b, 0x39, X64_RAX, X64_RBX); // cmp rax, rbx
        addCase(out, "int", "cmp", "r64,r64", b, kPairs[i][0], kPairs[i][1], F_ALL6);
    }
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; eRI8(b, 7, X64_RAX, (U8)kPairs[i][1]); // cmp rax, imm8
        addCase(out, "int", "cmp", "r64,imm8", b, kPairs[i][0], kPairs[i][1], F_ALL6);
    }
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; eRR(b, 0x85, X64_RAX, X64_RBX); // test rax, rbx
        addCase(out, "int", "test", "r64,r64", b, kPairs[i][0], kPairs[i][1],
                F_CF | F_PF | F_ZF | F_SF | F_OF);
    }
    // inc / dec (CF preserved)
    for (int fi = 0; fi < 2; fi++) {
        U32 fl = fi ? 0x203 : 0x202;
        for (int i = 0; i < kNumPairs; i++) {
            std::vector<U8> b; eGrp(b, 0xFF, 0, X64_RAX); // inc rax
            TestCase tc; tc.set = "int"; tc.name = "inc"; tc.form = "r64";
            tc.code = b; initRegs(tc, kPairs[i][0], kPairs[i][1]);
            tc.flagsIn = fl;
            tc.flagsMask = F_PF | F_AF | F_ZF | F_SF | F_OF;
            tc.checkCfPreserved = true;
            out.push_back(tc);
        }
        for (int i = 0; i < kNumPairs; i++) {
            std::vector<U8> b; eGrp(b, 0xFF, 1, X64_RAX); // dec rax
            TestCase tc; tc.set = "int"; tc.name = "dec"; tc.form = "r64";
            tc.code = b; initRegs(tc, kPairs[i][0], kPairs[i][1]);
            tc.flagsIn = fl;
            tc.flagsMask = F_PF | F_AF | F_ZF | F_SF | F_OF;
            tc.checkCfPreserved = true;
            out.push_back(tc);
        }
    }
    // neg / not
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; eGrp(b, 0xF7, 3, X64_RAX); // neg rax
        addCase(out, "int", "neg", "r64", b, kPairs[i][0], kPairs[i][1], F_ALL6);
    }
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; eGrp(b, 0xF7, 2, X64_RAX); // not rax
        addCase(out, "int", "not", "r64", b, kPairs[i][0], kPairs[i][1], 0);
    }
    // lea
    {
        U64 bases[] = {0x1000, 0x7FFFF000, 0xFFFFFFFFFFFFF000ULL, 0};
        for (U64 base : bases) { // lea rax, [rbx + 0x1234]
            std::vector<U8> b;
            eRex(b, true, X64_RAX, X64_RBX); b.push_back(0x8D);
            eModRM(b, 2, X64_RAX, X64_RBX); eU32(b, 0x1234);
            TestCase tc; tc.set = "int"; tc.name = "lea"; tc.form = "r64,[r64+disp32]";
            tc.code = b; initRegs(tc, 0, base); tc.flagsMask = 0;
            out.push_back(tc);
        }
        for (int i = 0; i < 4; i++) { // lea rax, [rbx + rcx*4 + 0x10]
            std::vector<U8> b;
            eRex(b, true, X64_RAX, X64_RBX); b.push_back(0x8D);
            eModRM(b, 2, X64_RAX, 4);
            b.push_back((U8)((2 << 6) | (X64_RCX << 3) | X64_RBX)); // SIB
            eU32(b, 0x10);
            TestCase tc; tc.set = "int"; tc.name = "lea"; tc.form = "r64,[r64+r64*4+disp32]";
            tc.code = b; initRegs(tc, 0, 0x1000);
            tc.r[X64_RCX] = (U64)(i * 0x100);
            tc.flagsMask = 0;
            out.push_back(tc);
        }
    }
    // shifts: shl/shr/sar, forms r64,1 / r64,cl / r64,imm8
    const char* shNames[] = {"shl", "shr", "sar"};
    int shExt[] = {4, 5, 7};
    U64 shCounts[] = {0, 1, 5, 31, 63, 64, 65};
    for (int s = 0; s < 3; s++) {
        { // r64,1
            std::vector<U8> b; eShift(b, 0xD1, shExt[s], X64_RAX);
            for (int i = 0; i < kNumPairs; i++)
                addCase(out, "int", shNames[s], "r64,1", b, kPairs[i][0], kPairs[i][1],
                        F_CF | F_PF | F_ZF | F_SF | F_OF);
        }
        { // r64,imm8
            U8 cnts[] = {1, 5, 31, 63};
            for (U8 c : cnts) {
                std::vector<U8> b; eShiftI(b, shExt[s], X64_RAX, c);
                char form[32]; snprintf(form, sizeof(form), "r64,%u", c);
                for (int i = 0; i < kNumPairs; i++)
                    addCase(out, "int", shNames[s], form, b, kPairs[i][0], kPairs[i][1],
                            c == 1 ? (F_CF | F_PF | F_ZF | F_SF | F_OF)
                                   : (F_CF | F_PF | F_ZF | F_SF));
            }
        }
        { // r64,cl
            for (U64 c : shCounts) {
                std::vector<U8> b; eShift(b, 0xD3, shExt[s], X64_RAX);
                char form[32]; snprintf(form, sizeof(form), "r64,cl=%llu",
                                        (unsigned long long)c);
                for (int i = 0; i < kNumPairs; i++) {
                    TestCase tc; tc.set = "int"; tc.name = shNames[s]; tc.form = form;
                    tc.code = b; initRegs(tc, kPairs[i][0], kPairs[i][1]);
                    tc.r[X64_RCX] = c;
                    if ((c & 63) == 0) {
                        tc.flagsMask = 0;
                        tc.checkFlagsUnchanged = true; // masked count 0: no flag change
                    } else if ((c & 63) == 1) {
                        tc.flagsMask = F_CF | F_PF | F_ZF | F_SF | F_OF;
                    } else {
                        tc.flagsMask = F_CF | F_PF | F_ZF | F_SF;
                    }
                    out.push_back(tc);
                }
            }
        }
    }
    // rotates: rol/ror/rcl/rcr, forms r64,1 / r64,imm8
    const char* rotNames[] = {"rol", "ror", "rcl", "rcr"};
    int rotExt[] = {0, 1, 2, 3};
    for (int s = 0; s < 4; s++) {
        bool isCarry = (s >= 2);
        U32 fis[] = {0x202, 0x203};
        int nfi = isCarry ? 2 : 1;
        { // r64,1
            std::vector<U8> b; eShift(b, 0xD1, rotExt[s], X64_RAX);
            for (int fi = 0; fi < nfi; fi++)
                for (int i = 0; i < kNumPairs; i++)
                    addCase(out, "int", rotNames[s], "r64,1", b,
                            kPairs[i][0], kPairs[i][1], F_CF | F_OF, fis[fi]);
        }
        { // r64,imm8
            U8 cnts[] = {1, 5, 33};
            for (U8 c : cnts) {
                std::vector<U8> b; eShiftI(b, rotExt[s], X64_RAX, c);
                char form[32]; snprintf(form, sizeof(form), "r64,%u", c);
                for (int fi = 0; fi < nfi; fi++)
                    for (int i = 0; i < kNumPairs; i++)
                        addCase(out, "int", rotNames[s], form, b,
                                kPairs[i][0], kPairs[i][1],
                                c == 1 ? (F_CF | F_OF) : F_CF, fis[fi]);
            }
        }
    }
    // shld / shrd r64,r64,imm8
    const char* shdNames[] = {"shld", "shrd"};
    U8 shdOps[] = {0xA4, 0xAC};
    for (int s = 0; s < 2; s++) {
        U8 cnts[] = {1, 8, 63};
        for (U8 c : cnts) {
            std::vector<U8> b;
            eRex(b, true, X64_RBX, X64_RAX);
            b.push_back(0x0F); b.push_back(shdOps[s]);
            eModRM(b, 3, X64_RBX, X64_RAX); b.push_back(c);
            char form[32]; snprintf(form, sizeof(form), "r64,r64,%u", c);
            for (int i = 0; i < kNumPairs; i++)
                addCase(out, "int", shdNames[s], form, b,
                        kPairs[i][0], kPairs[i][1], F_CF | F_PF | F_ZF | F_SF);
        }
    }
    // mul / imul
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; eGrp(b, 0xF7, 4, X64_RBX); // mul rbx
        TestCase tc; tc.set = "int"; tc.name = "mul"; tc.form = "r64";
        tc.code = b; initRegs(tc, kPairs[i][0], kPairs[i][1]);
        tc.flagsMask = F_CF | F_OF;
        out.push_back(tc);
    }
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; eGrp(b, 0xF7, 5, X64_RBX); // imul rbx
        TestCase tc; tc.set = "int"; tc.name = "imul"; tc.form = "r64";
        tc.code = b; initRegs(tc, kPairs[i][0], kPairs[i][1]);
        tc.flagsMask = F_CF | F_OF;
        out.push_back(tc);
    }
    for (int i = 0; i < kNumPairs; i++) { // imul rax, rbx
        std::vector<U8> b;
        eRex(b, true, X64_RAX, X64_RBX);
        b.push_back(0x0F); b.push_back(0xAF); eModRM(b, 3, X64_RAX, X64_RBX);
        addCase(out, "int", "imul", "r64,r64", b, kPairs[i][0], kPairs[i][1],
                F_CF | F_OF);
    }
    // div / idiv (hand-picked #DE-free triples)
    {
        struct Triple { U64 rax, rdx, rbx; };
        Triple divs[] = {
            {100, 0, 7}, {0, 0, 1}, {0xFFFFFFFFFFFFFFFFULL, 0, 0xFFFFFFFFFFFFFFFFULL},
            {0x123456789ABCDEF0ULL, 0, 0x100000}, {0, 5, 7}, {1, 0, 1},
        };
        for (auto t : divs) {
            std::vector<U8> b; eGrp(b, 0xF7, 6, X64_RBX); // div rbx
            TestCase tc; tc.set = "int"; tc.name = "div"; tc.form = "r64";
            tc.code = b; initRegs(tc, t.rax, t.rbx); tc.r[X64_RDX] = t.rdx;
            tc.flagsMask = 0;
            out.push_back(tc);
        }
        Triple idivs[] = { // (rax, rdx=sign-ext, rbx); quotient must fit in s64
            {(U64)-100, (U64)-1, 7}, {100, 0, 7}, {(U64)-1, (U64)-1, 1},
            {0x7FFFFFFFFFFFFFFFULL, 0, 1}, {(U64)-0x7FFFFFFFFFFFFFFFLL - 1, (U64)-1, 2},
            {0, 0, 3},
        };
        for (auto t : idivs) {
            std::vector<U8> b; eGrp(b, 0xF7, 7, X64_RBX); // idiv rbx
            TestCase tc; tc.set = "int"; tc.name = "idiv"; tc.form = "r64";
            tc.code = b; initRegs(tc, t.rax, t.rbx); tc.r[X64_RDX] = t.rdx;
            tc.flagsMask = 0;
            out.push_back(tc);
        }
    }
    // xchg / xadd
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; eRR(b, 0x87, X64_RAX, X64_RBX); // xchg rax, rbx
        addCase(out, "int", "xchg", "r64,r64", b, kPairs[i][0], kPairs[i][1], 0);
    }
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; // xadd rax, rbx
        eRex(b, true, X64_RBX, X64_RAX);
        b.push_back(0x0F); b.push_back(0xC1); eModRM(b, 3, X64_RBX, X64_RAX);
        addCase(out, "int", "xadd", "r64,r64", b, kPairs[i][0], kPairs[i][1], F_ALL6);
    }
    // bswap
    for (int i = 0; i < kNumPairs; i++) {
        std::vector<U8> b; // bswap rbx
        eRex(b, true, 0, X64_RBX); b.push_back(0x0F);
        b.push_back((U8)(0xC8 + (X64_RBX & 7)));
        addCase(out, "int", "bswap", "r64", b, kPairs[i][0], kPairs[i][1], 0);
    }
    // movsx / movzx
    {
        U64 vals[] = {0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF,
                      0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFFULL};
        for (U64 v : vals) { // movsx rax, ebx
            std::vector<U8> b;
            eRex(b, true, X64_RAX, X64_RBX); b.push_back(0x63);
            eModRM(b, 3, X64_RAX, X64_RBX);
            addCase(out, "int", "movsx", "r64,r32", b, 0, v, 0);
        }
        for (U64 v : vals) { // movzx rax, bx
            std::vector<U8> b;
            eRex(b, true, X64_RAX, X64_RBX); b.push_back(0x0F); b.push_back(0xB7);
            eModRM(b, 3, X64_RAX, X64_RBX);
            addCase(out, "int", "movzx", "r64,r16", b, 0, v, 0);
        }
        for (U64 v : vals) { // movzx rax, bl
            std::vector<U8> b;
            eRex(b, true, X64_RAX, X64_RBX); b.push_back(0x0F); b.push_back(0xB6);
            eModRM(b, 3, X64_RAX, X64_RBX);
            addCase(out, "int", "movzx", "r64,r8", b, 0, v, 0);
        }
        for (U64 v : vals) { // movsx rax, bl
            std::vector<U8> b;
            eRex(b, true, X64_RAX, X64_RBX); b.push_back(0x0F); b.push_back(0xBE);
            eModRM(b, 3, X64_RAX, X64_RBX);
            addCase(out, "int", "movsx", "r64,r8", b, 0, v, 0);
        }
    }
    // cbw/cwde/cdqe/cdq/cqo
    {
        struct Conv { const char* name; std::vector<U8> code; };
        std::vector<U8> cbw = {0x66, 0x98}, cwde = {0x98}, cdq = {0x99};
        std::vector<U8> cdqe, cqo;
        { eRex(cdqe, true, 0, 0); cdqe.push_back(0x98); }
        { eRex(cqo, true, 0, 0); cqo.push_back(0x99); }
        Conv convs[] = {{"cbw", cbw}, {"cwde", cwde}, {"cdqe", cdqe},
                        {"cdq", cdq}, {"cqo", cqo}};
        U64 vals[] = {0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF,
                      0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFFULL,
                      0x123456789ABCDEF0ULL};
        for (auto& c : convs)
            for (U64 v : vals)
                addCase(out, "int", c.name, "implicit", c.code, v, 0, 0);
    }
    // setcc (flags in -> al)
    {
        struct CC { const char* name; U8 cc; };
        CC ccs[] = {{"sete", 4}, {"setne", 5}, {"setb", 2}, {"setae", 3},
                    {"setl", 12}, {"setge", 13}, {"setle", 14}, {"setg", 15}};
        U32 flPats[] = {0x202, 0x203, 0x246, 0x282, 0x882, 0x886};
        for (auto& c : ccs)
            for (U32 fl : flPats) {
                std::vector<U8> b; // setcc al
                b.push_back(0x0F); b.push_back((U8)(0x90 + c.cc));
                eModRM(b, 3, 0, X64_RAX);
                TestCase tc; tc.set = "int"; tc.name = c.name; tc.form = "r8";
                tc.code = b; initRegs(tc, 0xAAAAAAAAAAAAAAAAULL, 0);
                tc.flagsIn = fl; tc.flagsMask = 0;
                out.push_back(tc);
            }
    }
    // cmovcc (flags in -> conditional move)
    {
        struct CC { const char* name; U8 cc; };
        CC ccs[] = {{"cmove", 4}, {"cmovne", 5}, {"cmovb", 2}, {"cmovae", 3},
                    {"cmovl", 12}, {"cmovge", 13}};
        U32 flPats[] = {0x202, 0x203, 0x246, 0x282, 0x882, 0x886};
        for (auto& c : ccs)
            for (U32 fl : flPats) {
                std::vector<U8> b; // cmovcc rax, rbx
                eRex(b, true, X64_RAX, X64_RBX);
                b.push_back(0x0F); b.push_back((U8)(0x40 + c.cc));
                eModRM(b, 3, X64_RAX, X64_RBX);
                TestCase tc; tc.set = "int"; tc.name = c.name; tc.form = "r64,r64";
                tc.code = b; initRegs(tc, 0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL);
                tc.flagsIn = fl; tc.flagsMask = 0;
                out.push_back(tc);
            }
    }
}

// ---------------------------------------------------------------------------
// FPU test builders (x87). Pattern: finit; fldX; fldY; fop_p; fstp [rbx].
// Result compared via 8 bytes at MEM_SCRATCH (rbx = MEM_SCRATCH).
// ---------------------------------------------------------------------------
static void buildFpuCases(std::vector<TestCase>& out) {
    struct Fop { const char* name; U8 b1, b2; }; // bytes after finit+loads
    // (loadA, loadB, op): st0=B, st1=A; fop_p computes st1 <op> st0, pops -> st0
    struct T { const char* name; U8 la, lb; U8 oa, ob; };
    T tests[] = {
        {"faddp", 0xE8, 0xE8, 0xDE, 0xC1}, // fld1,fld1,faddp -> 2.0
        {"faddp", 0xEE, 0xE8, 0xDE, 0xC1}, // fldz,fld1,faddp -> 1.0
        {"fsubp", 0xE8, 0xE8, 0xDE, 0xE9}, // 0.0
        {"fsubp", 0xE8, 0xEE, 0xDE, 0xE9}, // fld1,fldz,fsubp -> 1-0 = 1
        {"fsubrp", 0xE8, 0xEE, 0xDE, 0xE1},// fld1,fldz,fsubrp -> 0-1 = -1
        {"fmulp", 0xE8, 0xE8, 0xDE, 0xC9}, // 1*1
        {"fdivp", 0xE8, 0xEE, 0xDE, 0xF9}, // 1/0 = inf
        {"fdivp", 0xE8, 0xE8, 0xDE, 0xF9}, // 1/1
        {"fdivrp", 0xE8, 0xEE, 0xDE, 0xF1},// fld1,fldz,fdivrp -> 0/1 = 0
        {"fxch", 0xE8, 0xEE, 0xD9, 0xC9},  // fld1,fldz,fxch -> st0=1.0
    };
    for (auto& t : tests) {
        std::vector<U8> b;
        b.push_back(0xDB); b.push_back(0xE3);       // finit
        b.push_back(0xD9); b.push_back(t.la);       // fld A
        b.push_back(0xD9); b.push_back(t.lb);       // fld B
        b.push_back(t.oa); b.push_back(t.ob);       // fop
        b.push_back(0xDD); b.push_back(0x1B);       // fstp qword [rbx]
        TestCase tc; tc.set = "fpu"; tc.name = t.name; tc.form = "st,stp";
        tc.code = b; initRegs(tc, 0, MEM_SCRATCH);
        tc.flagsMask = 0; tc.checkMem = true; tc.memLen = 8;
        out.push_back(tc);
    }
    // bare loads (finit; fldX; fstp [rbx])
    struct L { const char* name; U8 l; };
    L loads[] = {{"fld1", 0xE8}, {"fldz", 0xEE}, {"fldpi", 0xEB},
                 {"fldl2t", 0xE9}, {"fldl2e", 0xEA}, {"fldlg2", 0xEC}, {"fldln2", 0xED}};
    for (auto& l : loads) {
        std::vector<U8> b;
        b.push_back(0xDB); b.push_back(0xE3);
        b.push_back(0xD9); b.push_back(l.l);
        b.push_back(0xDD); b.push_back(0x1B);
        TestCase tc; tc.set = "fpu"; tc.name = l.name; tc.form = "st";
        tc.code = b; initRegs(tc, 0, MEM_SCRATCH);
        tc.flagsMask = 0; tc.checkMem = true; tc.memLen = 8;
        out.push_back(tc);
    }
}

// ---------------------------------------------------------------------------
// SSE test builders. Pattern: movups xmm0,[rbx]; movups xmm1,[rbx+16];
// <op> xmm0,xmm1; movups [rbx],xmm0. 16 bytes at MEM_SCRATCH compared.
// ---------------------------------------------------------------------------
static void buildSseCases(std::vector<TestCase>& out) {
    struct Sop { const char* name; std::vector<U8> prefix; U8 op; bool isInt; };
    Sop ops[] = {
        {"movaps", {}, 0x28, false}, {"movups", {}, 0x10, false},
        {"addps", {}, 0x58, false}, {"subps", {}, 0x5C, false},
        {"mulps", {}, 0x59, false}, {"divps", {}, 0x5E, false},
        {"maxps", {}, 0x5F, false}, {"minps", {}, 0x5D, false},
        {"sqrtps", {}, 0x51, false},
        {"andps", {}, 0x54, false}, {"andnps", {}, 0x55, false},
        {"orps", {}, 0x56, false}, {"xorps", {}, 0x57, false},
        {"movdqa", {0x66}, 0x6F, true}, {"movdqu", {0xF3}, 0x6F, true},
        {"paddd", {0x66}, 0xFE, true}, {"psubd", {0x66}, 0xFA, true},
        {"paddq", {0x66}, 0xD4, true}, {"psubq", {0x66}, 0xFB, true},
        {"pmullw", {0x66}, 0xD5, true}, {"pcmpeqd", {0x66}, 0x76, true},
        {"pand", {0x66}, 0xDB, true}, {"por", {0x66}, 0xEB, true},
        {"pxor", {0x66}, 0xEF, true},
    };
    // float vectors: A={1,2,3,4}, B={0.5,-1,8,0.25}
    float fA[] = {1.0f, 2.0f, 3.0f, 4.0f};
    float fB[] = {0.5f, -1.0f, 8.0f, 0.25f};
    // int vectors
    U32 iA[] = {1, 2, 3, 4};
    U32 iB[] = {0xFFFFFFFF, 0x80000000, 5, 6};
    for (auto& o : ops) {
        std::vector<U8> b;
        b.push_back(0x0F); b.push_back(0x10); eModRM(b, 0, 0, 3);       // movups xmm0,[rbx]
        b.push_back(0x0F); b.push_back(0x10); eModRM(b, 1, 1, 3); b.push_back(16); // xmm1,[rbx+16]
        for (U8 p : o.prefix) b.push_back(p);
        b.push_back(0x0F); b.push_back(o.op); eModRM(b, 3, 0, 1);      // op xmm0,xmm1
        b.push_back(0x0F); b.push_back(0x11); eModRM(b, 0, 0, 3);      // movups [rbx],xmm0
        TestCase tc; tc.set = "sse"; tc.name = o.name; tc.form = "xmm,xmm";
        tc.code = b; initRegs(tc, 0, MEM_SCRATCH);
        tc.flagsMask = 0; tc.checkMem = true; tc.memLen = 16;
        if (o.isInt) {
            memcpy(tc.memInit, iA, 16); memcpy(tc.memInit + 16, iB, 16);
        } else {
            memcpy(tc.memInit, fA, 16); memcpy(tc.memInit + 16, fB, 16);
        }
        out.push_back(tc);
    }
    // float edge vectors: inf/-inf/nan/0 vs 1.0 (NaN-payload differential)
    {
        float fA2[] = {1.0f / 0.0f, -1.0f / 0.0f,
                       std::numeric_limits<float>::quiet_NaN(), 0.0f};
        float fB2[] = {1.0f, 1.0f, 1.0f, 1.0f};
        const char* names[] = {"addps", "mulps"};
        U8 ops2[] = {0x58, 0x59};
        for (int k = 0; k < 2; k++) {
            std::vector<U8> b;
            b.push_back(0x0F); b.push_back(0x10); eModRM(b, 0, 0, 3);
            b.push_back(0x0F); b.push_back(0x10); eModRM(b, 1, 1, 3); b.push_back(16);
            b.push_back(0x0F); b.push_back(ops2[k]); eModRM(b, 3, 0, 1);
            b.push_back(0x0F); b.push_back(0x11); eModRM(b, 0, 0, 3);
            TestCase tc; tc.set = "sse"; tc.name = names[k]; tc.form = "xmm,xmm(inf/nan)";
            tc.code = b; initRegs(tc, 0, MEM_SCRATCH);
            tc.flagsMask = 0; tc.checkMem = true; tc.memLen = 16;
            memcpy(tc.memInit, fA2, 16); memcpy(tc.memInit + 16, fB2, 16);
            out.push_back(tc);
        }
    }
}

// ---------------------------------------------------------------------------
// Timing specs: one per (set,name,form)-ish. setup runs each iteration before
// the instruction; baseline loop runs setup only. cost = (full-base)/N.
// ---------------------------------------------------------------------------
struct TimingSpec {
    std::string set, name, form;
    std::vector<U8> setup, insn;
    U64 r[16];
    U32 flagsIn = 0x202;
    U8 memInit[256] = {};
    bool needsMem = false;
};
static const int TIMING_N = 200000;

static std::vector<U8> loopWrap(const std::vector<U8>& setup,
                                const std::vector<U8>& insn) {
    std::vector<U8> b;
    b.push_back(0x41); b.push_back(0xBF); // mov r15d, imm32
    U32 n = TIMING_N;
    for (int i = 0; i < 4; i++) b.push_back((U8)(n >> (i * 8)));
    b.insert(b.end(), setup.begin(), setup.end());
    b.insert(b.end(), insn.begin(), insn.end());
    b.push_back(0x41); b.push_back(0xFF); b.push_back(0xCF); // dec r15d
    b.push_back(0x75); // jnz rel8
    b.push_back((U8)(0 - (2 + (int)insn.size() + (int)setup.size() + 3)));
    return b;
}

static void tInit(TimingSpec& t) {
    for (int i = 0; i < 16; i++) t.r[i] = guardReg(i);
    t.r[X64_RAX] = 0x123456789ABCDEF0ULL;
    t.r[X64_RBX] = 0x0FEDCBA987654321ULL;
    t.r[X64_RCX] = 5;
    t.r[X64_RDX] = 0;
    t.r[X64_RSP] = RSP_INIT;
    t.r[X64_R15] = 0; // overwritten by loopWrap's mov r15d
}

static void buildTimings(std::vector<TimingSpec>& out) {
    auto add = [&](const char* set, const char* name, const char* form,
                   const std::vector<U8>& setup, const std::vector<U8>& insn) {
        TimingSpec t; t.set = set; t.name = name; t.form = form;
        t.setup = setup; t.insn = insn; tInit(t);
        out.push_back(t);
    };
    // int: canonical r64,r64 forms (rax,rbx)
    auto rr = [](U8 op, std::vector<U8>& b) { eRR(b, op, X64_RAX, X64_RBX); };
    std::vector<U8> b;
    const char* aluN[] = {"add","sub","adc","sbb","and","or","xor"};
    U8 aluOp[] = {0x01,0x29,0x11,0x19,0x21,0x09,0x31};
    for (int i = 0; i < 7; i++) { b.clear(); rr(aluOp[i], b); add("int", aluN[i], "r64,r64", {}, b); }
    b.clear(); rr(0x39, b); add("int", "cmp", "r64,r64", {}, b);
    b.clear(); rr(0x85, b); add("int", "test", "r64,r64", {}, b);
    b.clear(); rr(0x89, b); add("int", "mov", "r64,r64", {}, b);
    b.clear(); { eRex(b, true, 0, X64_RAX); b.push_back(0xB8 + X64_RAX);
                 for (int k = 0; k < 8; k++) b.push_back(0x12); }
    add("int", "mov", "r64,imm64", {}, b);
    b.clear(); b.push_back(0x50); b.push_back(0x5B); add("int", "push", "r64+pop r64", {}, b);
    b.clear(); eGrp(b, 0xFF, 0, X64_RAX); add("int", "inc", "r64", {}, b);
    b.clear(); eGrp(b, 0xFF, 1, X64_RAX); add("int", "dec", "r64", {}, b);
    b.clear(); eGrp(b, 0xF7, 3, X64_RAX); add("int", "neg", "r64", {}, b);
    b.clear(); eGrp(b, 0xF7, 2, X64_RAX); add("int", "not", "r64", {}, b);
    b.clear(); eShift(b, 0xC1, 4, X64_RAX); b.push_back(5); add("int", "shl", "r64,imm8", {}, b);
    b.clear(); eShift(b, 0xC1, 5, X64_RAX); b.push_back(5); add("int", "shr", "r64,imm8", {}, b);
    b.clear(); eShift(b, 0xC1, 7, X64_RAX); b.push_back(5); add("int", "sar", "r64,imm8", {}, b);
    b.clear(); eShift(b, 0xD3, 4, X64_RAX); add("int", "shl", "r64,cl", {}, b);
    b.clear(); eShift(b, 0xD1, 0, X64_RAX); add("int", "rol", "r64,1", {}, b);
    b.clear(); eShift(b, 0xD1, 2, X64_RAX); add("int", "rcl", "r64,1", {}, b);
    b.clear(); // shld rax,rbx,8
    { eRex(b, true, X64_RBX, X64_RAX); b.push_back(0x0F); b.push_back(0xA4);
      eModRM(b, 3, X64_RBX, X64_RAX); b.push_back(8); }
    add("int", "shld", "r64,r64,imm8", {}, b);
    // mul/div need per-iteration re-init (they clobber rdx:rax)
    {
        std::vector<U8> setup, insn;
        eRex(setup, true, 0, X64_RAX); setup.push_back((U8)(0xB8 + X64_RAX));
        U64 v = 0x123456789ULL; for (int k = 0; k < 8; k++) setup.push_back((U8)(v >> (k*8)));
        eGrp(insn, 0xF7, 4, X64_RBX);
        add("int", "mul", "r64", setup, insn);
    }
    {
        // imul rax,rbx: re-init rax each iteration (imul clobbers rax)
        std::vector<U8> setup, insn;
        U64 v = 0x123456789ULL;
        eRex(setup, true, 0, X64_RAX); setup.push_back((U8)(0xB8 + X64_RAX));
        for (int k = 0; k < 8; k++) setup.push_back((U8)(v >> (k*8)));
        eRex(insn, true, X64_RAX, X64_RBX);
        insn.push_back(0x0F); insn.push_back(0xAF); eModRM(insn, 3, X64_RAX, X64_RBX);
        add("int", "imul", "r64,r64", setup, insn);
    }
    {
        std::vector<U8> setup, insn; // div rbx, rax re-init + rdx=0 each iter
        eRex(setup, true, 0, X64_RAX); setup.push_back((U8)(0xB8 + X64_RAX));
        U64 v = 100; for (int k = 0; k < 8; k++) setup.push_back((U8)(v >> (k*8)));
        setup.push_back(0x31); eModRM(setup, 3, 2, 2); // xor edx,edx
        eGrp(insn, 0xF7, 6, X64_RBX);
        TimingSpec t; t.set="int"; t.name="div"; t.form="r64";
        t.setup=setup; t.insn=insn; tInit(t); t.r[X64_RBX]=7;
        out.push_back(t);
    }
    b.clear(); eRR(b, 0x87, X64_RAX, X64_RBX); add("int", "xchg", "r64,r64", {}, b);
    b.clear(); { eRex(b, true, X64_RBX, X64_RAX); b.push_back(0x0F); b.push_back(0xC1);
                 eModRM(b, 3, X64_RBX, X64_RAX); }
    add("int", "xadd", "r64,r64", {}, b);
    b.clear(); { eRex(b, true, 0, X64_RAX); b.push_back(0x0F);
                 b.push_back((U8)(0xC8 + (X64_RAX & 7))); }
    add("int", "bswap", "r64", {}, b);
    b.clear(); { eRex(b, true, X64_RAX, X64_RBX); b.push_back(0x63);
                 eModRM(b, 3, X64_RAX, X64_RBX); }
    add("int", "movsx", "r64,r32", {}, b);
    b.clear(); { eRex(b, true, 0, 0); b.push_back(0x98); } // cdqe
    add("int", "cdqe", "implicit", {}, b);
    b.clear(); { eRex(b, true, 0, 0); b.push_back(0x99); } // cqo
    add("int", "cqo", "implicit", {}, b);
    b.clear(); b.push_back(0x0F); b.push_back(0x94); eModRM(b, 3, 0, X64_RAX); // sete al
    add("int", "sete", "r8", {}, b);
    b.clear(); { eRex(b, true, X64_RAX, X64_RBX); b.push_back(0x0F); b.push_back(0x44);
                 eModRM(b, 3, X64_RAX, X64_RBX); } // cmove rax,rbx
    add("int", "cmove", "r64,r64", {}, b);
    b.clear(); // lea rax,[rbx+0x1234]
    { eRex(b, true, X64_RAX, X64_RBX); b.push_back(0x8D);
      eModRM(b, 2, X64_RAX, X64_RBX); eU32(b, 0x1234); }
    add("int", "lea", "r64,[r64+disp32]", {}, b);
    // fpu: finit;fld1;fld1 / faddp;fstp st0  (balanced stack)
    {
        std::vector<U8> setup = {0xDB,0xE3, 0xD9,0xE8, 0xD9,0xE8};
        std::vector<U8> insn = {0xDE,0xC1, 0xDD,0xD8};
        add("fpu", "faddp", "st,stp", setup, insn);
    }
    // sse: loads / addps
    {
        std::vector<U8> setup, insn;
        setup.push_back(0x0F); setup.push_back(0x10); eModRM(setup, 0, 0, 3);
        setup.push_back(0x0F); setup.push_back(0x10); eModRM(setup, 1, 1, 3);
        setup.push_back(16);
        insn.push_back(0x0F); insn.push_back(0x58); eModRM(insn, 3, 0, 1);
        TimingSpec t; t.set="sse"; t.name="addps"; t.form="xmm,xmm";
        t.setup=setup; t.insn=insn; tInit(t); t.r[X64_RBX]=MEM_SCRATCH; t.needsMem=true;
        float fA[]={1,2,3,4}, fB[]={0.5f,-1,8,0.25f};
        memcpy(t.memInit, fA, 16); memcpy(t.memInit+16, fB, 16);
        out.push_back(t);
    }
    {
        std::vector<U8> setup, insn;
        setup.push_back(0x0F); setup.push_back(0x10); eModRM(setup, 0, 0, 3);
        setup.push_back(0x0F); setup.push_back(0x10); eModRM(setup, 1, 1, 3);
        setup.push_back(16);
        insn.push_back(0x66); insn.push_back(0x0F); insn.push_back(0xFE);
        eModRM(insn, 3, 0, 1); // paddd
        TimingSpec t; t.set="sse"; t.name="paddd"; t.form="xmm,xmm";
        t.setup=setup; t.insn=insn; tInit(t); t.r[X64_RBX]=MEM_SCRATCH; t.needsMem=true;
        U32 iA[]={1,2,3,4}, iB[]={0xFFFFFFFF,0x80000000,5,6};
        memcpy(t.memInit, iA, 16); memcpy(t.memInit+16, iB, 16);
        out.push_back(t);
    }
}

static double measureTiming(const TimingSpec& t) {
    std::vector<U8> full = loopWrap(t.setup, t.insn);
    std::vector<U8> base = loopWrap(t.setup, {});
    double best = 1e30;
    for (int rep = 0; rep < 2; rep++) {
        EmuResult a = runEmu(full.data(), full.size(), t.r, t.flagsIn,
                             t.needsMem ? t.memInit : nullptr, 20000000);
        EmuResult b2 = runEmu(base.data(), base.size(), t.r, t.flagsIn,
                              t.needsMem ? t.memInit : nullptr, 20000000);
        if (!a.ok || !b2.ok) return -1;
        double ns = (double)((long long)a.elapsedNs - (long long)b2.elapsedNs) / TIMING_N;
        if (ns < best) best = ns;
    }
    return best;
}

// ---------------------------------------------------------------------------
// Differ: one native run + three emulator runs per case.
// ---------------------------------------------------------------------------
static const char* regName(int i) {
    static const char* n[] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
                              "r8","r9","r10","r11","r12","r13","r14","r15"};
    return n[i];
}
static std::string hexBytes(const std::vector<U8>& b) {
    char tmp[4]; std::string s;
    for (U8 x : b) { snprintf(tmp, sizeof(tmp), "%02x", x); s += tmp; }
    return s;
}

struct CaseResult {
    int idx = -1;
    bool nativeOk = false;
    std::string nativeErr;
    bool emuOk = false;
    std::string emuErr;
    bool emuStable = true;
    U64 nRegs[16] = {}, eRegs[16] = {};
    U32 nFlags = 0, eFlags = 0;
    U8 nMem[256] = {}, eMem[256] = {};
    bool match = false;
    std::string mismatchDesc;
};

static CaseResult diffCase(const TestCase& tc, int idx, const NativeResult& n) {
    CaseResult cr; cr.idx = idx;
    if (!n.ok) {
        cr.nativeErr = n.timeout ? "native timeout"
                     : n.crashed ? "native crashed"
                     : "native setup failed";
        return cr;
    }
    cr.nativeOk = true;
    memcpy(cr.nRegs, n.regs, sizeof(cr.nRegs)); cr.nFlags = n.flags;
    memcpy(cr.nMem, n.scratch, sizeof(cr.nMem));

    EmuResult e[3];
    for (int k = 0; k < 3; k++)
        e[k] = runEmu(tc.code.data(), tc.code.size(), tc.r, tc.flagsIn,
                      tc.checkMem ? tc.memInit : nullptr);
    if (!e[0].ok) { cr.emuErr = e[0].err; return cr; }
    cr.emuOk = true;
    for (int k = 1; k < 3; k++) {
        if (!e[k].ok ||
            memcmp(e[k].regs, e[0].regs, sizeof(e[0].regs)) != 0 ||
            e[k].flags != e[0].flags ||
            (tc.checkMem && memcmp(e[k].scratch, e[0].scratch, (size_t)tc.memLen) != 0))
            cr.emuStable = false;
    }
    memcpy(cr.eRegs, e[0].regs, sizeof(cr.eRegs)); cr.eFlags = e[0].flags;
    memcpy(cr.eMem, e[0].scratch, sizeof(cr.eMem));

    bool rbxPatched = (tc.checkMem || tc.set == "fpu" || tc.set == "sse");
    std::string d;
    char tmp[128];
    // rsp: numeric addresses differ per side; require per-side balance instead
    if (cr.nRegs[X64_RSP] != n.rspIn) {
        snprintf(tmp, sizeof(tmp), "native-rsp-unbalanced ");
        d += tmp;
    }
    if (cr.eRegs[X64_RSP] != RSP_SNIPPET) {
        snprintf(tmp, sizeof(tmp), "emu-rsp=0x%llx(want 0x%llx) ",
                 (unsigned long long)cr.eRegs[X64_RSP],
                 (unsigned long long)RSP_SNIPPET);
        d += tmp;
    }
    // rbx holds the scratch address when patched: per-side preservation check
    if (rbxPatched) {
        if (cr.nRegs[X64_RBX] != n.rbxIn) d += "native-rbx-clobbered ";
        if (cr.eRegs[X64_RBX] != MEM_SCRATCH) d += "emu-rbx-clobbered ";
    }
    for (int i = 0; i < 16; i++) {
        if (i == X64_RSP) continue;
        if (rbxPatched && i == X64_RBX) continue;
        if (cr.nRegs[i] != cr.eRegs[i]) {
            snprintf(tmp, sizeof(tmp), "%s:n=0x%llx/e=0x%llx ",
                     regName(i), (unsigned long long)cr.nRegs[i],
                     (unsigned long long)cr.eRegs[i]);
            d += tmp;
        }
    }
    U32 nf = cr.nFlags & tc.flagsMask, ef = cr.eFlags & tc.flagsMask;
    if (nf != ef) {
        snprintf(tmp, sizeof(tmp), "flags(mask 0x%x):n=0x%x/e=0x%x ",
                 tc.flagsMask, nf, ef);
        d += tmp;
    }
    if (tc.checkMem && memcmp(cr.nMem, cr.eMem, (size_t)tc.memLen) != 0)
        d += "scratch-mem ";
    // native sanity + emulator check for CF preservation
    if (tc.checkCfPreserved) {
        if (((cr.nFlags ^ tc.flagsIn) & F_CF) != 0) d += "NATIVE-CF-CHANGED(test-bug?) ";
        if (((cr.eFlags ^ tc.flagsIn) & F_CF) != 0) d += "cf-not-preserved ";
    }
    if (tc.checkFlagsUnchanged) {
        if (((cr.nFlags ^ tc.flagsIn) & F_ALL6) != 0) d += "NATIVE-FLAGS-CHANGED(test-bug?) ";
        if (((cr.eFlags ^ tc.flagsIn) & F_ALL6) != 0) d += "flags-changed ";
    }
    cr.match = d.empty();
    cr.mismatchDesc = d;
    return cr;
}

// ---------------------------------------------------------------------------
// Aggregation + reporting.
// ---------------------------------------------------------------------------
struct FormAgg {
    std::string set, name, form, bytesHex;
    int total = 0, pass = 0, broken = 0, inconsistent = 0, nativeFail = 0;
    double emuNs = -1; // emulator ns per instruction (<0 = unmeasured)
    std::vector<std::string> failDescs; // up to 5
};

static std::string formKey(const TestCase& tc) {
    return tc.set + "|" + tc.name + "|" + tc.form;
}

static void writeJson(const std::string& path,
                      const std::vector<TestCase>& cases,
                      const std::vector<CaseResult>& results,
                      const std::vector<FormAgg>& aggs) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    fprintf(f, "{\n\"cases\": [\n");
    for (size_t i = 0; i < cases.size(); i++) {
        const TestCase& tc = cases[i];
        const CaseResult& cr = results[i];
        fprintf(f, "%s{\"idx\":%zu,\"set\":\"%s\",\"name\":\"%s\",\"form\":\"%s\",\"bytes\":\"%s\","
                   "\"native_ok\":%s,\"emu_ok\":%s,\"emu_stable\":%s,\"match\":%s,"
                   "\"native_flags\":\"0x%x\",\"emu_flags\":\"0x%x\",\"flags_mask\":\"0x%x\","
                   "\"emu_err\":\"%s\",\"mismatch\":\"%s\"",
                i ? ",\n" : "", i, tc.set.c_str(), tc.name.c_str(), tc.form.c_str(),
                hexBytes(tc.code).c_str(),
                cr.nativeOk ? "true" : "false", cr.emuOk ? "true" : "false",
                cr.emuStable ? "true" : "false", cr.match ? "true" : "false",
                cr.nFlags, cr.eFlags, tc.flagsMask,
                cr.emuErr.c_str(), cr.mismatchDesc.c_str());
        if (tc.checkMem && cr.nativeOk && cr.emuOk) {
            fprintf(f, ",\"scratch_n\":\"");
            for (int k = 0; k < tc.memLen; k++) fprintf(f, "%02x", cr.nMem[k]);
            fprintf(f, "\",\"scratch_e\":\"");
            for (int k = 0; k < tc.memLen; k++) fprintf(f, "%02x", cr.eMem[k]);
            fprintf(f, "\"");
        }
        fprintf(f, "}");
    }
    fprintf(f, "\n],\n\"forms\": [\n");
    for (size_t i = 0; i < aggs.size(); i++) {
        const FormAgg& a = aggs[i];
        fprintf(f, "%s{\"set\":\"%s\",\"name\":\"%s\",\"form\":\"%s\",\"bytes\":\"%s\","
                   "\"total\":%d,\"pass\":%d,\"broken\":%d,\"inconsistent\":%d,"
                   "\"native_fail\":%d,\"emu_ns\":%.2f}",
                i ? ",\n" : "", a.set.c_str(), a.name.c_str(), a.form.c_str(),
                a.bytesHex.c_str(), a.total, a.pass, a.broken, a.inconsistent,
                a.nativeFail, a.emuNs);
    }
    fprintf(f, "\n]}\n");
    fclose(f);
}

int main(int argc, char** argv) {
    std::string outDir = "test-results/cpu-differential";
    g_runnerPath = "runner/runner.elf";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) outDir = argv[++i];
        if (!strcmp(argv[i], "--runner") && i + 1 < argc) g_runnerPath = argv[++i];
        if (!strcmp(argv[i], "--qemu") && i + 1 < argc) g_qemuPath = argv[++i];
    }

    std::vector<TestCase> cases;
    buildIntCases(cases);
    buildFpuCases(cases);
    buildSseCases(cases);
    std::vector<TimingSpec> timings;
    buildTimings(timings);

    printf("cpu-diff: %zu cases, %zu timing specs\n", cases.size(), timings.size());
    printf("runner: %s via %s\n", g_runnerPath.c_str(), g_qemuPath.c_str());
    fflush(stdout);

    // ---- differential runs: native in batches, emulator per case ----
    std::vector<int> idxs(cases.size());
    for (size_t i = 0; i < cases.size(); i++) idxs[i] = (int)i;
    std::vector<NativeResult> nativeRes;
    {
        // batch in chunks so one qemu invocation stays bounded
        const size_t CHUNK = 64;
        for (size_t s = 0; s < idxs.size(); s += CHUNK) {
            size_t e = std::min(idxs.size(), s + CHUNK);
            std::vector<int> sub(idxs.begin() + s, idxs.begin() + e);
            std::vector<NativeResult> r = runNativeBatch(cases, sub);
            nativeRes.insert(nativeRes.end(), r.begin(), r.end());
            printf("  native batch %zu-%zu done\n", s, e);
            fflush(stdout);
        }
    }
    std::vector<CaseResult> results;
    results.reserve(cases.size());
    for (size_t i = 0; i < cases.size(); i++) {
        results.push_back(diffCase(cases[i], (int)i, nativeRes[i]));
        if ((i + 1) % 100 == 0) { printf("  ... %zu/%zu\n", i + 1, cases.size()); fflush(stdout); }
    }

    // ---- timing runs (emulator only; the box is ARM64 — no x86-64 hardware
    // for native timing, and qemu TCG timing is not cycle-meaningful) ----
    struct TR { std::string set, name, form; double emuNs; double relToMedian; };
    std::vector<TR> trs;
    for (size_t i = 0; i < timings.size(); i++) {
        const TimingSpec& t = timings[i];
        double emu = measureTiming(t);
        trs.push_back({t.set, t.name, t.form, emu, -1});
        printf("  timing %s/%s/%s: emu=%.2fns/insn\n",
               t.set.c_str(), t.name.c_str(), t.form.c_str(), emu);
        fflush(stdout);
    }

    // ---- aggregate per (set,name,form) ----
    std::vector<FormAgg> aggs;
    std::vector<std::string> order;
    for (size_t i = 0; i < cases.size(); i++) {
        std::string k = formKey(cases[i]);
        auto it = std::find(order.begin(), order.end(), k);
        if (it == order.end()) { order.push_back(k); aggs.push_back(FormAgg()); }
        FormAgg& a = aggs[std::find(order.begin(), order.end(), k) - order.begin()];
        if (a.total == 0) {
            a.set = cases[i].set; a.name = cases[i].name;
            a.form = cases[i].form; a.bytesHex = hexBytes(cases[i].code);
        }
        const CaseResult& cr = results[i];
        a.total++;
        if (!cr.nativeOk) { a.nativeFail++; continue; }
        if (!cr.emuOk || !cr.match) {
            a.broken++;
            if (a.failDescs.size() < 5)
                a.failDescs.push_back(cr.emuOk ? cr.mismatchDesc
                                               : std::string("emu-error: ") + cr.emuErr);
        } else if (!cr.emuStable) {
            a.inconsistent++;
        } else {
            a.pass++;
        }
    }
    for (auto& a : aggs)
        for (auto& tr : trs)
            if (tr.set == a.set && tr.name == a.name && tr.form == a.form) { a.emuNs = tr.emuNs; break; }

    // ---- four-bucket summary per set ----
    struct Buckets { int total = 0, pass = 0, slowflaky = 0, broken = 0; };
    std::map<std::string, Buckets> buckets;
    // Slow threshold: emulator-relative. The box is ARM64, so no x86-64
    // hardware exists for a native/emulator ratio; qemu TCG timing is not
    // cycle-meaningful. Instead: slow = emu ns/insn > 4x the set median
    // (see METHOD.md for justification).
    std::map<std::string, std::vector<double>> nsVals;
    for (auto& a : aggs) if (a.emuNs > 0) nsVals[a.set].push_back(a.emuNs);
    std::map<std::string, double> slowThresh;
    for (auto& kv : nsVals) {
        auto& v = kv.second;
        std::sort(v.begin(), v.end());
        double median = v[v.size() / 2];
        double thr = 4.0 * median;
        slowThresh[kv.first] = thr;
        printf("set %-4s: n=%zu median_emu_ns=%.1f slow_threshold=%.1f\n",
               kv.first.c_str(), v.size(), median, thr);
    }
    // classify: broken > inconsistent > slow > pass
    std::map<std::string, int> slowCount; // per set, for the summary
    for (auto& a : aggs) {
        Buckets& b = buckets[a.set];
        b.total++;
        if (a.broken > 0 || a.nativeFail > 0) { b.broken++; continue; }
        bool flaky = a.inconsistent > 0;
        bool slow = false;
        auto it = slowThresh.find(a.set);
        if (it != slowThresh.end() && a.emuNs > it->second) { slow = true; slowCount[a.set]++; }
        if (flaky || slow) { b.slowflaky++; continue; }
        b.pass++;
    }

    // ---- write outputs ----
    std::string mk = "mkdir -p " + outDir;
    (void)system(mk.c_str());
    writeJson(outDir + "/report.json", cases, results, aggs);

    FILE* md = fopen((outDir + "/REPORT.md").c_str(), "w");
    fprintf(md, "# CPU differential report\n\n");
    fprintf(md, "Generated by tools/cpu-diff (see METHOD.md). "
                "Each snippet runs natively (x86-64 under qemu-x86_64 — the box is ARM64) "
                "and in the CPU64 interpreter; "
                "registers, defined flags, and scratch memory are diffed. "
                "Timing is emulator-only: loop of %d iterations, baseline-subtracted, "
                "min of 2 runs, reported as ns per instruction.\n\n",
            TIMING_N);
    for (auto& kv : buckets) {
        const std::string& set = kv.first;
        Buckets& b = kv.second;
        fprintf(md, "## Set: %s\n\n", set.c_str());
        fprintf(md, "| bucket | count |\n|---|---|\n");
        fprintf(md, "| total instruction forms tested | %d |\n", b.total);
        fprintf(md, "| fully passing (correct vs native) | %d |\n", b.pass);
        fprintf(md, "| passing but slow or inconsistent | %d |\n", b.slowflaky);
        fprintf(md, "| fully broken (wrong results) | %d |\n", b.broken);
        fprintf(md, "\nSlow threshold for this set: emulator ns/insn > %.1f "
                    "(4x median; no x86-64 hardware on this ARM64 box, so the "
                    "threshold is emulator-relative — see METHOD.md).\n\n",
                slowThresh[set]);
    }
    // slowest 20
    std::vector<FormAgg*> byNs;
    for (auto& a : aggs) if (a.emuNs > 0) byNs.push_back(&a);
    std::sort(byNs.begin(), byNs.end(),
              [](FormAgg* x, FormAgg* y) { return x->emuNs > y->emuNs; });
    fprintf(md, "## Slowest 20 by emulator ns/insn\n\n");
    fprintf(md, "| # | set | instruction | form | bytes | emu ns/insn |\n|---|---|---|---|---|---|\n");
    for (size_t i = 0; i < byNs.size() && i < 20; i++)
        fprintf(md, "| %zu | %s | %s | %s | `%s` | %.1f |\n", i + 1,
                byNs[i]->set.c_str(), byNs[i]->name.c_str(),
                byNs[i]->form.c_str(), byNs[i]->bytesHex.c_str(), byNs[i]->emuNs);
    fprintf(md, "\n## Failures (broken forms)\n\n");
    fprintf(md, "| set | instruction | form | bytes | cases | detail |\n"
                "|---|---|---|---|---|---|\n");
    for (auto& a : aggs) {
        if (a.broken == 0 && a.nativeFail == 0) continue;
        std::string det;
        for (auto& d : a.failDescs) { det += d; det += " / "; }
        if (a.nativeFail) det += "NATIVE-RUNNER-FAILURE ";
        fprintf(md, "| %s | %s | %s | `%s` | %d broken / %d total | %s |\n",
                a.set.c_str(), a.name.c_str(), a.form.c_str(),
                a.bytesHex.c_str(), a.broken, a.total, det.c_str());
    }
    fprintf(md, "\n## Inconsistent forms (correct but flaky across emulator runs)\n\n");
    fprintf(md, "| set | instruction | form | bytes | flaky cases |\n|---|---|---|---|---|\n");
    for (auto& a : aggs)
        if (a.inconsistent > 0 && a.broken == 0)
            fprintf(md, "| %s | %s | %s | `%s` | %d |\n",
                    a.set.c_str(), a.name.c_str(), a.form.c_str(),
                    a.bytesHex.c_str(), a.inconsistent);
    fprintf(md, "\n## Raw per-form table (auditable)\n\n");
    fprintf(md, "| set | instruction | form | bytes | native result | emulator result | "
                "emu ns/insn | verdict |\n|---|---|---|---|---|---|---|---|\n");
    for (auto& a : aggs) {
        const char* verdict;
        if (a.broken > 0 || a.nativeFail > 0) verdict = "BROKEN";
        else if (a.inconsistent > 0) verdict = "INCONSISTENT";
        else {
            auto it = slowThresh.find(a.set);
            verdict = (it != slowThresh.end() && a.emuNs > it->second) ? "SLOW" : "PASS";
        }
        char nss[32]; snprintf(nss, sizeof(nss), "%.1f", a.emuNs);
        fprintf(md, "| %s | %s | %s | `%s` | %s | %s | %s | %s |\n",
                a.set.c_str(), a.name.c_str(), a.form.c_str(), a.bytesHex.c_str(),
                a.broken ? "differs (see Failures)" : "matches",
                a.broken ? "differs (see Failures)" : "matches",
                a.emuNs > 0 ? nss : "n/a", verdict);
    }
    fclose(md);

    printf("\n==== FOUR-BUCKET SUMMARY ====\n");
    for (auto& kv : buckets) {
        Buckets& b = kv.second;
        printf("set=%s total=%d pass=%d slow_or_inconsistent=%d broken=%d\n",
               kv.first.c_str(), b.total, b.pass, b.slowflaky, b.broken);
    }
    printf("report: %s/REPORT.md  raw: %s/report.json\n", outDir.c_str(), outDir.c_str());
    return 0;
}

#else
// Built without BOXEDWINE_GUEST_X64: the CPU64 core is compiled out.
#include <cstdio>
int main() {
    printf("cpu-diff requires -DBOXEDWINE_GUEST_X64=1 (CPU64 core compiled out)\n");
    return 1;
}
#endif
