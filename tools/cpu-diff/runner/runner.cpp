// runner.cpp — x86-64 native snippet runner (static ELF, runs under qemu-x86_64).
// Usage: runner.elf <input.bin> <output.bin>
// Batch protocol: input = U32 ncases, then ncases * NativeCase; the runner
// forks one child per case (crash isolation), each child runs its snippet
// via the native_run trampoline and reports regs/flags/scratch.
// Output = U32 ncases, then ncases * NativeResult.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef uint64_t U64;
typedef uint32_t U32;
typedef uint8_t U8;

extern "C" {
U64 g_natSavedRsp = 0;
U8* g_natCode = nullptr;
U64 g_natRegsOut[16] = {};
U32 g_natFlagsOut = 0;
U64 g_natSavedRegs[6] = {}; // rbx, rbp, r12, r13, r14, r15 (callee-saved)
U64 g_natTmpR10 = 0;        // stash for r10's test value across the transfer
void native_run(U8* code, U64* regs_in, U64 flags_in);
}

__asm__(
".text\n"
".globl native_run\n"
".type native_run, @function\n"
"native_run:\n"                        // rdi=code rsi=regs_in rdx=flags_in
"    mov %rsp, g_natSavedRsp(%rip)\n"  // save original stack
"    sub $128, %rsp\n"                 // keep clear of the caller's red zone
"    mov %rbx, g_natSavedRegs+0(%rip)\n" // save callee-saved regs
"    mov %rbp, g_natSavedRegs+8(%rip)\n"
"    mov %r12, g_natSavedRegs+16(%rip)\n"
"    mov %r13, g_natSavedRegs+24(%rip)\n"
"    mov %r14, g_natSavedRegs+32(%rip)\n"
"    mov %r15, g_natSavedRegs+40(%rip)\n"
"    mov %rdi, g_natCode(%rip)\n"       // stash code pointer
"    push %rdx\n"
"    popfq\n"                          // flags_in -> rflags
"    mov %rsi, %r10\n"                 // r10 = regs_in base
"    mov 88(%r10), %r11\n"             // r11..r15
"    mov 96(%r10), %r12\n"
"    mov 104(%r10), %r13\n"
"    mov 112(%r10), %r14\n"
"    mov 120(%r10), %r15\n"
"    mov 24(%r10), %rbx\n"             // rbx (3*8)
"    mov 40(%r10), %rbp\n"             // rbp (5*8)
"    mov 64(%r10), %r8\n"              // r8
"    mov 72(%r10), %r9\n"              // r9
"    mov 48(%r10), %rsi\n"             // rsi (6*8)
"    mov 56(%r10), %rdi\n"             // rdi (7*8)
"    mov 8(%r10), %rcx\n"              // rcx (1*8)
"    mov 16(%r10), %rdx\n"             // rdx (2*8)
"    mov 80(%r10), %rax\n"             // rax = r10's test value (temp)
"    mov %rax, g_natTmpR10(%rip)\n"    // stash it
"    mov 0(%r10), %rax\n"              // rax = test rax
"    mov 32(%r10), %r10\n"             // r10 = regs_in[4] (rsp value)
"    mov %r10, %rsp\n"                 // switch to snippet stack
"    lea native_ret(%rip), %r10\n"
"    push %r10\n"                      // return address for the snippet's ret
"    mov g_natTmpR10(%rip), %r10\n"    // r10 = test r10 value
"    jmp *g_natCode(%rip)\n"           // enter snippet (ends with ret)
"native_ret:\n"
"    mov %rax, g_natRegsOut+0(%rip)\n" // capture all 16 regs
"    mov %rcx, g_natRegsOut+8(%rip)\n"
"    mov %rdx, g_natRegsOut+16(%rip)\n"
"    mov %rbx, g_natRegsOut+24(%rip)\n"
"    mov %rsp, g_natRegsOut+32(%rip)\n"
"    mov %rbp, g_natRegsOut+40(%rip)\n"
"    mov %rsi, g_natRegsOut+48(%rip)\n"
"    mov %rdi, g_natRegsOut+56(%rip)\n"
"    mov %r8, g_natRegsOut+64(%rip)\n"
"    mov %r9, g_natRegsOut+72(%rip)\n"
"    mov %r10, g_natRegsOut+80(%rip)\n"
"    mov %r11, g_natRegsOut+88(%rip)\n"
"    mov %r12, g_natRegsOut+96(%rip)\n"
"    mov %r13, g_natRegsOut+104(%rip)\n"
"    mov %r14, g_natRegsOut+112(%rip)\n"
"    mov %r15, g_natRegsOut+120(%rip)\n"
"    pushfq\n"
"    pop %rax\n"
"    mov %eax, g_natFlagsOut(%rip)\n"
"    mov g_natSavedRegs+0(%rip), %rbx\n" // restore callee-saved regs
"    mov g_natSavedRegs+8(%rip), %rbp\n"
"    mov g_natSavedRegs+16(%rip), %r12\n"
"    mov g_natSavedRegs+24(%rip), %r13\n"
"    mov g_natSavedRegs+32(%rip), %r14\n"
"    mov g_natSavedRegs+40(%rip), %r15\n"
"    mov g_natSavedRsp(%rip), %rsp\n"   // restore original stack
"    ret\n"
);

struct NativeCase {
    U64 codeLen = 0;
    U8 code[4096] = {};
    U64 regsIn[16] = {};
    U32 flagsIn = 0;
    U32 patchRbx = 0; // if 1, regsIn[3] (rbx) = scratch address
    U8 scratchIn[256] = {};
};

struct NativeResult {
    U32 ok = 0;       // ran to completion
    U32 crashed = 0;  // died by signal
    U32 timeout = 0;
    U32 pad = 0;
    U64 regs[16] = {};
    U32 flags = 0;
    U32 pad2 = 0;
    U64 rspIn = 0, rbxIn = 0;
    U8 scratch[256] = {};
    U64 elapsedNs = 0;
};

static bool readAll(int fd, void* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, (char*)buf + off, n - off);
        if (r <= 0) return false;
        off += (size_t)r;
    }
    return true;
}
static bool writeAll(int fd, const void* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = write(fd, (const char*)buf + off, n - off);
        if (r <= 0) return false;
        off += (size_t)r;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: runner <in> <out>\n"); return 2; }
    int fdin = open(argv[1], O_RDONLY);
    if (fdin < 0) { perror("open in"); return 2; }
    U32 ncases = 0;
    if (!readAll(fdin, &ncases, 4)) return 2;
    if (ncases > 4096) { fprintf(stderr, "too many cases\n"); return 2; }

    int fdout = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fdout < 0) { perror("open out"); return 2; }
    writeAll(fdout, &ncases, 4);

    for (U32 ci = 0; ci < ncases; ci++) {
        NativeCase c;
        memset(&c, 0, sizeof(c));
        if (!readAll(fdin, &c, sizeof(c))) { fprintf(stderr, "short read\n"); return 2; }
        NativeResult r;
        memset(&r, 0, sizeof(r));

        int pipefd[2];
        if (pipe(pipefd) != 0) return 2;
        pid_t pid = fork();
        if (pid < 0) return 2;
        if (pid == 0) {
            // ---- child: run one snippet ----
            close(pipefd[0]);
            U8* stack = (U8*)mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            U8* scratch = (U8*)mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            U8* code = (U8*)mmap(nullptr, c.codeLen + 1, PROT_READ | PROT_WRITE | PROT_EXEC,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (stack == MAP_FAILED || scratch == MAP_FAILED || code == MAP_FAILED) _exit(43);
            memcpy(code, c.code, c.codeLen);
            code[c.codeLen] = 0xC3; // ret
            memcpy(scratch, c.scratchIn, 256);
            U64 regs[16];
            memcpy(regs, c.regsIn, sizeof(regs));
            regs[4] = (U64)(stack + 0x1000 - 8); // rsp init (16-aligned post-call)
            if (c.patchRbx) regs[3] = (U64)scratch;
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            native_run(code, regs, c.flagsIn);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            NativeResult cr;
            memset(&cr, 0, sizeof(cr));
            cr.ok = 1;
            memcpy(cr.regs, g_natRegsOut, sizeof(cr.regs));
            cr.flags = g_natFlagsOut;
            cr.rspIn = regs[4];
            cr.rbxIn = regs[3];
            memcpy(cr.scratch, scratch, 256);
            cr.elapsedNs = (U64)(t1.tv_sec - t0.tv_sec) * 1000000000ULL +
                           (U64)(t1.tv_nsec - t0.tv_nsec);
            writeAll(pipefd[1], &cr, sizeof(cr));
            _exit(0);
        }
        // ---- parent: collect with timeout ----
        close(pipefd[1]);
        int wstatus = 0, waited = 0;
        pid_t w = 0;
        while (waited < 5000) {
            w = waitpid(pid, &wstatus, WNOHANG);
            if (w == pid || w < 0) break;
            usleep(1000);
            waited++;
        }
        if (w != pid) {
            kill(pid, SIGKILL);
            waitpid(pid, &wstatus, 0);
            r.timeout = 1;
        } else if (WIFSIGNALED(wstatus)) {
            r.crashed = 1;
        } else if (WEXITSTATUS(wstatus) == 0) {
            if (!readAll(pipefd[0], &r, sizeof(r))) r.crashed = 1;
        } else {
            r.crashed = 1;
        }
        close(pipefd[0]);
        writeAll(fdout, &r, sizeof(r));
    }
    close(fdin);
    close(fdout);
    return 0;
}
