#include "boxedwine.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __TEST
#ifdef BOXEDWINE_GUEST_X64

#include "kerror.h"
#include "kstat.h"
#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

// Guest tests for three register filings, measured against the host
// at the raw syscall level (test-results/fd139-152/hostref-fd139-152*.c):
//   - the 64-bit dispatcher's fstatfs case ignored its descriptor
//     and answered the fabricated statistics for any value; the host
//     answers -EBADF for a descriptor that is not open, ahead of even
//     the buffer fault, and accepts any open descriptor.
//   - utimensat accepted flag bits outside
//     AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH on non-null paths; the
//     kernel validates the flags word ahead of the name fetch and
//     answers -EINVAL (the times copy-in still precedes it).
//   - a path whose prefix component is not a directory answered
//     -ENOENT where the host answers -ENOTDIR, at every path call
//     routed through the shared resolver.
// Fixture tree (guest root = a fresh native temp dir):
//   /a.txt      regular file
//   /d          directory, /d/inner a regular file inside it
//   /ln         symlink to a.txt (a file)
//   /ld         symlink to d (a directory)
// cwd is "/".

namespace {

struct Fixture64 {
    KProcessPtr process;
    KMemory64* memory = nullptr;
    std::unique_ptr<CPU64> cpu;
    std::shared_ptr<FsFileNode> previousRoot;
    bool ok = false;
    FD fileFd = (FD)-1;
    FD dirFd = (FD)-1;

    static constexpr U64 dataPage = 0x7E000000ULL;
    // Guest string / buffer slots on the fixture page.
    static constexpr U64 absAt        = dataPage + 0x000; // "/a.txt"
    static constexpr U64 absChildAt   = dataPage + 0x080; // "/a.txt/child"
    static constexpr U64 childAt      = dataPage + 0x100; // "a.txt/child"
    static constexpr U64 missingChild = dataPage + 0x180; // "missing/child"
    static constexpr U64 lnChildAt    = dataPage + 0x200; // "ln/child"
    static constexpr U64 ldInnerAt    = dataPage + 0x280; // "ld/inner"
    static constexpr U64 dInnerAt     = dataPage + 0x300; // "d/inner"
    static constexpr U64 aTxtAt       = dataPage + 0x380; // "a.txt"
    static constexpr U64 emptyAt      = dataPage + 0x400; // ""
    static constexpr U64 innerAt      = dataPage + 0x480; // "inner"
    static constexpr U64 trailAt      = dataPage + 0x500; // "a.txt/"
    static constexpr U64 dotAt        = dataPage + 0x580; // "a.txt/."
    static constexpr U64 dotdotAt     = dataPage + 0x600; // "a.txt/.."
    static constexpr U64 xAt          = dataPage + 0x680; // "x"
    static constexpr U64 tgtAt        = dataPage + 0x700; // "t"
    static constexpr U64 missingAt    = dataPage + 0x780; // "no-such"
    static constexpr U64 bufAt        = dataPage + 0x800; // out structs
    static constexpr U64 timesAt      = dataPage + 0x900; // two timespecs
    static constexpr U64 nameAt       = dataPage + 0x980; // "user.x"

    Fixture64() {
        TestContext& context = testContext();
        process = context.process;
        if (!process->memory64) process->memory64 = new KMemory64(process.get());
        memory = process->memory64;

        char dirTemplate[] = "/tmp/bw64fd152XXXXXX";
        char* dir = mkdtemp(dirTemplate);
        if (!dir) return;
        previousRoot = Fs::rootNode;
        Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
        Fs::clearPathCache();

        KFileDescriptorPtr fileDesc, innerDesc, dirDesc;
        if (process->openFile(B("/"), B("/a.txt"), K_O_RDWR | K_O_CREAT, fileDesc) != 0 || !fileDesc ||
            process->mkdir(B("/d")) != 0 ||
            process->openFile(B("/"), B("/d/inner"), K_O_RDWR | K_O_CREAT, innerDesc) != 0 || !innerDesc ||
            process->openFile(B("/"), B("/d"), K_O_RDONLY, dirDesc) != 0 || !dirDesc ||
            process->symlink(B("a.txt"), B("/ln")) != 0 ||
            process->symlink(B("d"), B("/ld")) != 0 ||
            process->chdir(B("/")) != 0) {
            return;
        }
        process->close(innerDesc->handle);
        fileFd = fileDesc->handle;
        dirFd = dirDesc->handle;

        if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
            memory->mmapAnonymousFixed(dataPage, K64_PAGE_SIZE, 3) != dataPage) {
            return;
        }
        cpu = std::make_unique<CPU64>(memory);
        cpu->thread = context.thread;

        memory->memcpyToGuest(absAt, "/a.txt", 7);
        memory->memcpyToGuest(absChildAt, "/a.txt/child", 13);
        memory->memcpyToGuest(childAt, "a.txt/child", 12);
        memory->memcpyToGuest(missingChild, "missing/child", 14);
        memory->memcpyToGuest(lnChildAt, "ln/child", 9);
        memory->memcpyToGuest(ldInnerAt, "ld/inner", 9);
        memory->memcpyToGuest(dInnerAt, "d/inner", 8);
        memory->memcpyToGuest(aTxtAt, "a.txt", 6);
        memory->memcpyToGuest(emptyAt, "", 1);
        memory->memcpyToGuest(innerAt, "inner", 6);
        memory->memcpyToGuest(trailAt, "a.txt/", 7);
        memory->memcpyToGuest(dotAt, "a.txt/.", 8);
        memory->memcpyToGuest(dotdotAt, "a.txt/..", 9);
        memory->memcpyToGuest(xAt, "x", 2);
        memory->memcpyToGuest(tgtAt, "t", 2);
        memory->memcpyToGuest(missingAt, "no-such", 8);
        memory->memcpyToGuest(nameAt, "user.x", 7);
        // A valid pair of timespecs (sec = 1500000000, nsec = 0).
        U8 times[32] = {};
        U64 sec = 1500000000ULL;
        memcpy(times, &sec, 8);
        memcpy(times + 16, &sec, 8);
        memory->memcpyToGuest(timesAt, times, 32);
        ok = true;
    }

    ~Fixture64() {
        if (previousRoot) {
            Fs::rootNode = previousRoot;
            Fs::clearPathCache();
        }
    }

    S64 invoke(U64 nr, U64 a1 = 0, U64 a2 = 0, U64 a3 = 0, U64 a4 = 0, U64 a5 = 0, U64 a6 = 0) {
        cpu->reg[X64_RAX].setU64(nr);
        cpu->reg[X64_RDI].setU64(a1);
        cpu->reg[X64_RSI].setU64(a2);
        cpu->reg[X64_RDX].setU64(a3);
        cpu->reg[X64_R10].setU64(a4);
        cpu->reg[X64_R8].setU64(a5);
        cpu->reg[X64_R9].setU64(a6);
        ksyscall64(cpu.get());
        return (S64)cpu->reg[X64_RAX].u64;
    }
};

void expectEq(S64 got, S64 want, const char* label) {
    if (got != want) {
        testFail("%s: got %lld, want %lld", label, (long long)got, (long long)want);
    }
}

constexpr U64 AT_FDCWD64 = (U64)(S64)-100;

} // namespace

void testFstatfsDescriptor64Syscalls() {
    Fixture64 f;
    if (!f.ok) {
        testFail("fstatfs test could not stage its fixture");
        return;
    }
    // Any open descriptor keeps the fabricated answer (f_type is the
    // tmpfs magic the shared helper writes).
    expectEq(f.invoke(138, f.fileFd, Fixture64::bufAt), 0, "fstatfs(open file fd)");
    if (f.memory->readq(Fixture64::bufAt) != 0x01021994) testFail("fstatfs(open file fd) did not write the fabricated f_type");
    expectEq(f.invoke(138, f.dirFd, Fixture64::bufAt), 0, "fstatfs(open dir fd)");

    // A descriptor that is not open answers -EBADF on the host.
    expectEq(f.invoke(138, 9999, Fixture64::bufAt), -K_EBADF, "fstatfs(unopened fd)");
    expectEq(f.invoke(138, (U64)-1, Fixture64::bufAt), -K_EBADF, "fstatfs(fd -1)");

    // A just-closed descriptor answers -EBADF (staged so no later
    // open can reuse the number before the call).
    KFileDescriptorPtr bDesc;
    if (f.process->openFile(B("/"), B("/b.txt"), K_O_RDWR | K_O_CREAT, bDesc) != 0 || !bDesc) {
        testFail("fstatfs test could not stage its closed-fd file");
        return;
    }
    FD closedFd = bDesc->handle;
    f.process->close(closedFd);
    expectEq(f.invoke(138, closedFd, Fixture64::bufAt), -K_EBADF, "fstatfs(just-closed fd)");

    // The descriptor verdict precedes the buffer fault; with an open
    // descriptor the null buffer still faults.
    expectEq(f.invoke(138, f.fileFd, 0), -K_EFAULT, "fstatfs(open fd, null buf)");
    expectEq(f.invoke(138, 9999, 0), -K_EBADF, "fstatfs(bad fd, null buf)");

    // The path form still resolves first and answers as before.
    expectEq(f.invoke(137, Fixture64::absAt, Fixture64::bufAt), 0, "statfs(/a.txt) control");
}

void testUtimensatFlags64Syscalls() {
    Fixture64 f;
    if (!f.ok) {
        testFail("utimensat flags test could not stage its fixture");
        return;
    }
    // Controls: the two documented flag bits in any combination.
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, Fixture64::timesAt, 0), 0, "utimensat(a.txt, flags 0)");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, Fixture64::timesAt, 0x100), 0, "utimensat(a.txt, AT_SYMLINK_NOFOLLOW)");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, Fixture64::timesAt, 0x1000), 0, "utimensat(a.txt, AT_EMPTY_PATH)");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, Fixture64::timesAt, 0x1100), 0, "utimensat(a.txt, both bits)");

    // Any other bit answers -EINVAL on the host, on every shape:
    // the kernel validates the flags word ahead of the name fetch.
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, Fixture64::timesAt, 0x2000), -K_EINVAL, "utimensat(a.txt, 0x2000)");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, Fixture64::timesAt, 0x1), -K_EINVAL, "utimensat(a.txt, 0x1)");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, Fixture64::timesAt, 0x800), -K_EINVAL, "utimensat(a.txt, 0x800)");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, Fixture64::timesAt, (U64)-1), -K_EINVAL, "utimensat(a.txt, all bits)");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::missingAt, Fixture64::timesAt, 0x2000), -K_EINVAL, "utimensat(missing, 0x2000): flags precede the name fetch");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::emptyAt, Fixture64::timesAt, 0x2000), -K_EINVAL, "utimensat(\"\", 0x2000): flags precede the empty-path answer");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, 0, 0x2000), -K_EINVAL, "utimensat(a.txt, NULL times, 0x2000)");
    expectEq(f.invoke(280, f.dirFd, Fixture64::innerAt, Fixture64::timesAt, 0x2000), -K_EINVAL, "utimensat(dirfd, inner, 0x2000)");

    // The times copy-in still precedes the flags verdict.
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::aTxtAt, 0x1000, 0x2000), -K_EFAULT, "utimensat(a.txt, unreadable times, 0x2000)");

    // The through-a-file shape reaches the same call: with valid
    // flags the host answers -ENOTDIR (measured at statfs and the
    // other path calls; utimensat shares the name walk).
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::childAt, Fixture64::timesAt, 0x2000), -K_EINVAL, "utimensat(a.txt/child, 0x2000): flags first");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::childAt, Fixture64::timesAt, 0), -K_ENOTDIR, "utimensat(a.txt/child, 0)");
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::absChildAt, Fixture64::timesAt, 0), -K_ENOTDIR, "utimensat(/a.txt/child, 0)");

    // Standing stub behaviour the filings do not cover: a plain
    // missing path and a dirfd-relative path keep the success
    // answer (the 64-bit stub does not model resolution or dirfds).
    expectEq(f.invoke(280, AT_FDCWD64, Fixture64::missingAt, Fixture64::timesAt, 0), 0, "utimensat(missing, 0) keeps the stub answer");
    expectEq(f.invoke(280, f.dirFd, Fixture64::innerAt, Fixture64::timesAt, 0), 0, "utimensat(dirfd, inner, 0) keeps the stub answer");
}

void testNotDir64Syscalls() {
    Fixture64 f;
    if (!f.ok) {
        testFail("ENOTDIR test could not stage its fixture");
        return;
    }
    const U64 child = Fixture64::childAt;         // "a.txt/child"
    const U64 absChild = Fixture64::absChildAt;   // "/a.txt/child"
    const U64 missingChild = Fixture64::missingChild; // "missing/child"
    const U64 lnChild = Fixture64::lnChildAt;     // "ln/child" (ln -> a.txt)
    const U64 aTxt = Fixture64::aTxtAt;           // "a.txt"
    const U64 buf = Fixture64::bufAt;

    // statfs (137) and the stat family.
    expectEq(f.invoke(137, child, buf), -K_ENOTDIR, "statfs(a.txt/child)");
    expectEq(f.invoke(137, absChild, buf), -K_ENOTDIR, "statfs(/a.txt/child)");
    expectEq(f.invoke(137, lnChild, buf), -K_ENOTDIR, "statfs(ln/child) through a symlink to a file");
    expectEq(f.invoke(137, missingChild, buf), -K_ENOENT, "statfs(missing/child) control");
    expectEq(f.invoke(137, aTxt, buf), 0, "statfs(a.txt) control");
    expectEq(f.invoke(4, child, buf), -K_ENOTDIR, "stat(a.txt/child)");
    expectEq(f.invoke(6, child, buf), -K_ENOTDIR, "lstat(a.txt/child)");
    expectEq(f.invoke(4, missingChild, buf), -K_ENOENT, "stat(missing/child) control");
    expectEq(f.invoke(262, AT_FDCWD64, child, buf, 0), -K_ENOTDIR, "newfstatat(a.txt/child)");
    expectEq(f.invoke(262, AT_FDCWD64, aTxt, buf, 0), 0, "newfstatat(a.txt) control");
    expectEq(f.invoke(332, AT_FDCWD64, child, 0, 0x7ff, buf), -K_ENOTDIR, "statx(a.txt/child)");
    expectEq(f.invoke(332, AT_FDCWD64, aTxt, 0, 0x7ff, buf), 0, "statx(a.txt) control");

    // openat (257): plain and create forms.
    expectEq(f.invoke(257, AT_FDCWD64, child, 0, 0), -K_ENOTDIR, "openat(a.txt/child, O_RDONLY)");
    expectEq(f.invoke(257, AT_FDCWD64, child, 0x41, 0644), -K_ENOTDIR, "openat(a.txt/child, O_WRONLY|O_CREAT)");
    expectEq(f.invoke(257, AT_FDCWD64, missingChild, 0, 0), -K_ENOENT, "openat(missing/child) control");
    S64 opened = f.invoke(257, AT_FDCWD64, aTxt, 0, 0);
    if (opened < 0) testFail("openat(a.txt) control failed: %lld", (long long)opened);
    else f.invoke(3, (U64)opened);

    // access family (21 / 269) and fchmodat (268).
    expectEq(f.invoke(21, child, 0), -K_ENOTDIR, "access(a.txt/child)");
    expectEq(f.invoke(21, missingChild, 0), -K_ENOENT, "access(missing/child) control");
    expectEq(f.invoke(269, AT_FDCWD64, child, 0, 0), -K_ENOTDIR, "faccessat(a.txt/child)");
    expectEq(f.invoke(268, AT_FDCWD64, child, 0644, 0), -K_ENOTDIR, "fchmodat(a.txt/child)");

    // xattr block (191 / 192): resolution precedes the attribute
    // verdicts, so the through-file shape answers -ENOTDIR and a
    // plain file with no attributes answers -ENODATA.
    expectEq(f.invoke(191, child, Fixture64::nameAt, buf, 256), -K_ENOTDIR, "getxattr(a.txt/child)");
    expectEq(f.invoke(192, child, Fixture64::nameAt, buf, 256), -K_ENOTDIR, "lgetxattr(a.txt/child)");
    expectEq(f.invoke(191, aTxt, Fixture64::nameAt, buf, 256), -K_ENODATA, "getxattr(a.txt) control");

    // readlink (89): only the through-file verdict moves; the
    // plain-missing answer is a separate standing divergence.
    expectEq(f.invoke(89, child, buf, 256), -K_ENOTDIR, "readlink(a.txt/child)");

    // Calls routed through the process layer.
    expectEq(f.invoke(83, child, 0755), -K_ENOTDIR, "mkdir(a.txt/child)");
    // rmdir (84) has no case at the 64-bit dispatcher at all (it
    // answers -ENOSYS, "unimplemented syscall #84"); the shape is
    // pinned at KProcess::rmdir on the 32-bit side instead. The
    // missing case is filed separately.
    expectEq(f.invoke(87, child), -K_ENOTDIR, "unlink(a.txt/child)");
    expectEq(f.invoke(90, child, 0644), -K_ENOTDIR, "chmod(a.txt/child)");
    expectEq(f.invoke(80, child), -K_ENOTDIR, "chdir(a.txt/child)");
    expectEq(f.invoke(80, aTxt), -K_ENOTDIR, "chdir(a.txt) control: a resolved file already answers -ENOTDIR");
    expectEq(f.invoke(82, aTxt, child), -K_ENOTDIR, "rename(a.txt, a.txt/child): destination through a file");
    expectEq(f.invoke(82, child, Fixture64::xAt), -K_ENOTDIR, "rename(a.txt/child, x): source through a file");
    expectEq(f.invoke(88, Fixture64::tgtAt, child), -K_ENOTDIR, "symlink(t, a.txt/child)");
    expectEq(f.invoke(86, aTxt, child), -K_ENOTDIR, "link(a.txt, a.txt/child)");
    expectEq(f.invoke(86, child, Fixture64::xAt), -K_ENOTDIR, "link(a.txt/child, x)");

    // inotify_add_watch (254) on an inotify descriptor (294).
    S64 ifd = f.invoke(294, 0);
    if (ifd < 0) {
        testFail("inotify_init1 failed: %lld", (long long)ifd);
    } else {
        expectEq(f.invoke(254, (U64)ifd, child, 0xfff), -K_ENOTDIR, "inotify_add_watch(a.txt/child)");
        if (f.invoke(254, (U64)ifd, aTxt, 0xfff) < 0) testFail("inotify_add_watch(a.txt) control failed");
    }

    // Printed observations, not pinned: the host answers -ENOTDIR
    // for these shapes too, but the resolver's path normalization
    // erases the trailing component before the walk runs, so they
    // are a distinct mechanism from the filed walk-miss shape.
    printf("FD-140 observation: statfs(\"a.txt/\") -> %lld (host: -ENOTDIR)\n",
           (long long)f.invoke(137, Fixture64::trailAt, buf));
    printf("FD-140 observation: statfs(\"a.txt/.\") -> %lld (host: -ENOTDIR)\n",
           (long long)f.invoke(137, Fixture64::dotAt, buf));
    printf("FD-140 observation: statfs(\"a.txt/..\") -> %lld (host: -ENOTDIR)\n",
           (long long)f.invoke(137, Fixture64::dotdotAt, buf));
}

#endif
#endif
