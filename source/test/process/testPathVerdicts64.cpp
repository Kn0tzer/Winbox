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
// at the raw syscall level (test-results/fd157-161/hostref-fd157-161.c,
// strace cross-checked against the libc forms):
//   - readlink/readlinkat answered -EINVAL for a path that does not
//     exist, the same answer as for a resolved non-link; the host
//     answers -ENOENT for the missing path and keeps -EINVAL for the
//     resolved non-link (FD-158).
//   - a path whose final component is erased by normalization
//     ("a.txt/", "a.txt/.", "a.txt/..") resolved to the file itself
//     where the host answers -ENOTDIR: the trailing marker steps
//     into the final component, so that component must be a
//     directory (FD-159).
//   - the 64-bit dispatcher's rmdir (84) had no case and answered
//     -ENOSYS for every path; the process layer implements the call
//     and the host answers by shape (FD-160).
// Fixture tree (guest root = a fresh native temp dir):
//   /a.txt      regular file
//   /d          directory, /d/inner a regular file inside it
//   /empty      empty directory
//   /empty2     empty directory
//   /ln         symlink to a.txt (a file)
//   /ld         symlink to d (a directory)
// cwd is "/".

namespace {

struct FixturePV64 {
    KProcessPtr process;
    KMemory64* memory = nullptr;
    std::unique_ptr<CPU64> cpu;
    std::shared_ptr<FsFileNode> previousRoot;
    bool ok = false;

    static constexpr U64 dataPage = 0x7E000000ULL;
    static constexpr U64 aTxtAt       = dataPage + 0x000; // "a.txt"
    static constexpr U64 dAt          = dataPage + 0x040; // "d"
    static constexpr U64 lnAt         = dataPage + 0x080; // "ln"
    static constexpr U64 missingAt    = dataPage + 0x0C0; // "missing"
    static constexpr U64 childAt      = dataPage + 0x100; // "a.txt/child"
    static constexpr U64 missingChild = dataPage + 0x140; // "missing/child"
    static constexpr U64 trailAt      = dataPage + 0x180; // "a.txt/"
    static constexpr U64 dotAt        = dataPage + 0x1C0; // "a.txt/."
    static constexpr U64 dotdotAt     = dataPage + 0x200; // "a.txt/.."
    static constexpr U64 trail2At     = dataPage + 0x240; // "a.txt//"
    static constexpr U64 dTrailAt     = dataPage + 0x280; // "d/"
    static constexpr U64 dDotAt       = dataPage + 0x2C0; // "d/."
    static constexpr U64 dDotdotAt    = dataPage + 0x300; // "d/.."
    static constexpr U64 ldTrailAt    = dataPage + 0x340; // "ld/"
    static constexpr U64 ldDotAt      = dataPage + 0x380; // "ld/."
    static constexpr U64 ldDotdotAt   = dataPage + 0x3C0; // "ld/.."
    static constexpr U64 lnTrailAt    = dataPage + 0x400; // "ln/"
    static constexpr U64 lnDotAt      = dataPage + 0x440; // "ln/."
    static constexpr U64 lnDotdotAt   = dataPage + 0x480; // "ln/.."
    static constexpr U64 missTrailAt  = dataPage + 0x4C0; // "missing/"
    static constexpr U64 missDotAt    = dataPage + 0x500; // "missing/."
    static constexpr U64 missDotdotAt = dataPage + 0x540; // "missing/.."
    static constexpr U64 dotSlashAt   = dataPage + 0x580; // "./"
    static constexpr U64 dotOnlyAt    = dataPage + 0x5C0; // "."
    static constexpr U64 dotdotOnlyAt = dataPage + 0x600; // ".."
    static constexpr U64 rootAt       = dataPage + 0x640; // "/"
    static constexpr U64 emptyAt      = dataPage + 0x680; // "empty"
    static constexpr U64 empty2TrailAt = dataPage + 0x6C0; // "empty2/"
    static constexpr U64 missMidAt    = dataPage + 0x700; // "missing/../a.txt"
    static constexpr U64 fileMidAt    = dataPage + 0x740; // "a.txt/../d"
    static constexpr U64 emptyStrAt   = dataPage + 0x780; // ""
    static constexpr U64 bufAt        = dataPage + 0x800; // out buffers
    static constexpr U64 bogusAt      = dataPage + 0xA00; // "bogus.x"
    static constexpr U64 trustedAt    = dataPage + 0xA40; // "trusted.x"
    static constexpr U64 nameUserAt   = dataPage + 0xA80; // "user.x"
    static constexpr U64 bareUserAt   = dataPage + 0xAC0; // "user."
    static constexpr U64 aclAt        = dataPage + 0xB00; // "system.posix_acl_access"
    static constexpr U64 systemAt     = dataPage + 0xB80; // "system.x"
    static constexpr U64 dMidAt       = dataPage + 0xBC0; // "d/../a.txt"
    static constexpr U64 ldMidAt      = dataPage + 0xC00; // "ld/../a.txt"
    static constexpr U64 lnMidAt      = dataPage + 0xC40; // "ln/../d"
    static constexpr U64 innerMidAt   = dataPage + 0xC80; // "d/inner/../../a.txt"
    static constexpr U64 deepMidAt    = dataPage + 0xCC0; // "d/../../a.txt"
    static constexpr U64 relRootMidAt = dataPage + 0xD00; // "../a.txt"
    static constexpr U64 absRootMidAt = dataPage + 0xD40; // "/../a.txt"

    FixturePV64() {
        TestContext& context = testContext();
        process = context.process;
        if (!process->memory64) process->memory64 = new KMemory64(process.get());
        memory = process->memory64;

        char dirTemplate[] = "/tmp/bw64pv64XXXXXX";
        char* dir = mkdtemp(dirTemplate);
        if (!dir) return;
        previousRoot = Fs::rootNode;
        Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
        Fs::clearPathCache();

        KFileDescriptorPtr fileDesc, innerDesc;
        if (process->openFile(B("/"), B("/a.txt"), K_O_RDWR | K_O_CREAT, fileDesc) != 0 || !fileDesc ||
            process->mkdir(B("/d")) != 0 ||
            process->openFile(B("/"), B("/d/inner"), K_O_RDWR | K_O_CREAT, innerDesc) != 0 || !innerDesc ||
            process->mkdir(B("/empty")) != 0 ||
            process->mkdir(B("/empty2")) != 0 ||
            process->symlink(B("a.txt"), B("/ln")) != 0 ||
            process->symlink(B("d"), B("/ld")) != 0 ||
            process->chdir(B("/")) != 0) {
            return;
        }
        process->close(fileDesc->handle);
        process->close(innerDesc->handle);

        if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
            memory->mmapAnonymousFixed(dataPage, K64_PAGE_SIZE, 3) != dataPage) {
            return;
        }
        cpu = std::make_unique<CPU64>(memory);
        cpu->thread = context.thread;

        memory->memcpyToGuest(aTxtAt, "a.txt", 6);
        memory->memcpyToGuest(dAt, "d", 2);
        memory->memcpyToGuest(lnAt, "ln", 3);
        memory->memcpyToGuest(missingAt, "missing", 8);
        memory->memcpyToGuest(childAt, "a.txt/child", 12);
        memory->memcpyToGuest(missingChild, "missing/child", 14);
        memory->memcpyToGuest(trailAt, "a.txt/", 7);
        memory->memcpyToGuest(dotAt, "a.txt/.", 8);
        memory->memcpyToGuest(dotdotAt, "a.txt/..", 9);
        memory->memcpyToGuest(trail2At, "a.txt//", 8);
        memory->memcpyToGuest(dTrailAt, "d/", 3);
        memory->memcpyToGuest(dDotAt, "d/.", 4);
        memory->memcpyToGuest(dDotdotAt, "d/..", 5);
        memory->memcpyToGuest(ldTrailAt, "ld/", 4);
        memory->memcpyToGuest(ldDotAt, "ld/.", 5);
        memory->memcpyToGuest(ldDotdotAt, "ld/..", 6);
        memory->memcpyToGuest(lnTrailAt, "ln/", 4);
        memory->memcpyToGuest(lnDotAt, "ln/.", 5);
        memory->memcpyToGuest(lnDotdotAt, "ln/..", 6);
        memory->memcpyToGuest(missTrailAt, "missing/", 9);
        memory->memcpyToGuest(missDotAt, "missing/.", 10);
        memory->memcpyToGuest(missDotdotAt, "missing/..", 11);
        memory->memcpyToGuest(dotSlashAt, "./", 3);
        memory->memcpyToGuest(dotOnlyAt, ".", 2);
        memory->memcpyToGuest(dotdotOnlyAt, "..", 3);
        memory->memcpyToGuest(rootAt, "/", 2);
        memory->memcpyToGuest(emptyAt, "empty", 6);
        memory->memcpyToGuest(empty2TrailAt, "empty2/", 8);
        memory->memcpyToGuest(missMidAt, "missing/../a.txt", 17);
        memory->memcpyToGuest(fileMidAt, "a.txt/../d", 11);
        memory->memcpyToGuest(emptyStrAt, "", 1);
        memory->memcpyToGuest(bogusAt, "bogus.x", 8);
        memory->memcpyToGuest(trustedAt, "trusted.x", 10);
        memory->memcpyToGuest(nameUserAt, "user.x", 7);
        memory->memcpyToGuest(bareUserAt, "user.", 6);
        memory->memcpyToGuest(aclAt, "system.posix_acl_access", 24);
        memory->memcpyToGuest(systemAt, "system.x", 9);
        memory->memcpyToGuest(dMidAt, "d/../a.txt", 11);
        memory->memcpyToGuest(ldMidAt, "ld/../a.txt", 12);
        memory->memcpyToGuest(lnMidAt, "ln/../d", 8);
        memory->memcpyToGuest(innerMidAt, "d/inner/../../a.txt", 20);
        memory->memcpyToGuest(deepMidAt, "d/../../a.txt", 14);
        memory->memcpyToGuest(relRootMidAt, "../a.txt", 9);
        memory->memcpyToGuest(absRootMidAt, "/../a.txt", 10);
        ok = true;
    }

    ~FixturePV64() {
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

void testReadlinkMissing64Syscalls() {
    FixturePV64 f;
    if (!f.ok) {
        testFail("readlink test could not stage its fixture");
        return;
    }
    const U64 buf = FixturePV64::bufAt;

    // The filed shape: a path that does not exist answers -ENOENT
    // on the host, not the -EINVAL a resolved non-link answers.
    expectEq(f.invoke(89, FixturePV64::missingAt, buf, 256), -K_ENOENT, "readlink(missing)");
    expectEq(f.invoke(267, AT_FDCWD64, FixturePV64::missingAt, buf, 256), -K_ENOENT, "readlinkat(missing)");
    expectEq(f.invoke(89, FixturePV64::missingChild, buf, 256), -K_ENOENT, "readlink(missing/child)");
    expectEq(f.invoke(267, AT_FDCWD64, FixturePV64::missingChild, buf, 256), -K_ENOENT, "readlinkat(missing/child)");

    // Controls: a resolved non-link keeps -EINVAL, through a file
    // keeps -ENOTDIR, and a real link still reads back its target.
    expectEq(f.invoke(89, FixturePV64::aTxtAt, buf, 256), -K_EINVAL, "readlink(a.txt) control");
    expectEq(f.invoke(89, FixturePV64::dAt, buf, 256), -K_EINVAL, "readlink(d) control");
    expectEq(f.invoke(89, FixturePV64::childAt, buf, 256), -K_ENOTDIR, "readlink(a.txt/child) control");
    S64 len = f.invoke(89, FixturePV64::lnAt, buf, 256);
    expectEq(len, 5, "readlink(ln) length control");
    if (len == 5) {
        char target[8] = {};
        f.memory->memcpyFromGuest(target, buf, 5);
        if (memcmp(target, "a.txt", 5) != 0) testFail("readlink(ln) target is not a.txt");
    }
}

void testTrailingNotDir64Syscalls() {
    FixturePV64 f;
    if (!f.ok) {
        testFail("trailing-shape test could not stage its fixture");
        return;
    }
    const U64 buf = FixturePV64::bufAt;

    // The filed shapes: the trailing marker steps into a regular
    // file, so the host answers -ENOTDIR where the guest resolved
    // the path to the file itself.
    expectEq(f.invoke(137, FixturePV64::trailAt, buf), -K_ENOTDIR, "statfs(a.txt/)");
    expectEq(f.invoke(137, FixturePV64::dotAt, buf), -K_ENOTDIR, "statfs(a.txt/.)");
    expectEq(f.invoke(137, FixturePV64::dotdotAt, buf), -K_ENOTDIR, "statfs(a.txt/..)");
    expectEq(f.invoke(137, FixturePV64::trail2At, buf), -K_ENOTDIR, "statfs(a.txt//)");
    expectEq(f.invoke(4, FixturePV64::trailAt, buf), -K_ENOTDIR, "stat(a.txt/)");
    expectEq(f.invoke(262, AT_FDCWD64, FixturePV64::dotAt, buf, 0), -K_ENOTDIR, "newfstatat(a.txt/.)");
    expectEq(f.invoke(89, FixturePV64::trailAt, buf, 256), -K_ENOTDIR, "readlink(a.txt/)");

    // The same shapes through a symlink to a file: the marker
    // steps into the link's target, which is not a directory.
    expectEq(f.invoke(137, FixturePV64::lnTrailAt, buf), -K_ENOTDIR, "statfs(ln/)");
    expectEq(f.invoke(137, FixturePV64::lnDotAt, buf), -K_ENOTDIR, "statfs(ln/.)");
    expectEq(f.invoke(137, FixturePV64::lnDotdotAt, buf), -K_ENOTDIR, "statfs(ln/..)");

    // Controls: the same markers through real directories and
    // through a symlink to a directory keep resolving.
    expectEq(f.invoke(137, FixturePV64::dTrailAt, buf), 0, "statfs(d/) control");
    expectEq(f.invoke(137, FixturePV64::dDotAt, buf), 0, "statfs(d/.) control");
    expectEq(f.invoke(137, FixturePV64::dDotdotAt, buf), 0, "statfs(d/..) control");
    expectEq(f.invoke(137, FixturePV64::ldTrailAt, buf), 0, "statfs(ld/) control");
    expectEq(f.invoke(137, FixturePV64::ldDotAt, buf), 0, "statfs(ld/.) control");
    expectEq(f.invoke(137, FixturePV64::ldDotdotAt, buf), 0, "statfs(ld/..) control");
    expectEq(f.invoke(137, FixturePV64::dotSlashAt, buf), 0, "statfs(./) control");
    expectEq(f.invoke(137, FixturePV64::dotOnlyAt, buf), 0, "statfs(.) control");
    expectEq(f.invoke(137, FixturePV64::rootAt, buf), 0, "statfs(/) control");
    // A bare ".." at the root is pinned by
    // testMidDotDot64Syscalls (FD-164).
    expectEq(f.invoke(89, FixturePV64::ldTrailAt, buf, 256), -K_EINVAL, "readlink(ld/): the marker steps through the link; its target is a directory, not a link");
    expectEq(f.invoke(89, FixturePV64::lnTrailAt, buf, 256), -K_ENOTDIR, "readlink(ln/): the marker steps through the link into a regular file");

    // A missing anchor keeps the missing answer on every spelling:
    // for the "/.." spelling the erased pair must not turn the
    // miss into a resolution of the working directory.
    expectEq(f.invoke(137, FixturePV64::missTrailAt, buf), -K_ENOENT, "statfs(missing/) control");
    expectEq(f.invoke(137, FixturePV64::missDotAt, buf), -K_ENOENT, "statfs(missing/.) control");
    expectEq(f.invoke(137, FixturePV64::missDotdotAt, buf), -K_ENOENT, "statfs(missing/..)");

    // The erased ".." pair in the MIDDLE of a path is pinned by
    // testMidDotDot64Syscalls (FD-164).
}

void testXattrNamespace64Syscalls() {
    FixturePV64 f;
    if (!f.ok) {
        testFail("xattr namespace test could not stage its fixture");
        return;
    }
    const U64 buf = FixturePV64::bufAt;

    // FD-163, host-measured at the raw syscall level
    // (test-results/fd162-164/hostref-fd162-164.out and
    // hostref-fd163b/c/d.out): the namespace verdict on a
    // resolved target -- an unhandled prefix answers -ENOTSUP,
    // a recognised namespace with an absent attribute -ENODATA,
    // a bare recognised prefix -EINVAL, an empty name -ERANGE
    // at fetch time ahead of the target verdicts.
    expectEq(f.invoke(191, FixturePV64::aTxtAt, FixturePV64::bogusAt, buf, 256), -K_ENOTSUP, "getxattr(a.txt, bogus.x)");
    expectEq(f.invoke(191, FixturePV64::aTxtAt, FixturePV64::trustedAt, buf, 256), -K_ENODATA, "getxattr(a.txt, trusted.x)");
    expectEq(f.invoke(191, FixturePV64::aTxtAt, FixturePV64::systemAt, buf, 256), -K_ENOTSUP, "getxattr(a.txt, system.x)");
    expectEq(f.invoke(191, FixturePV64::aTxtAt, FixturePV64::aclAt, buf, 256), -K_ENODATA, "getxattr(a.txt, system.posix_acl_access)");
    expectEq(f.invoke(191, FixturePV64::aTxtAt, FixturePV64::bareUserAt, buf, 256), -K_EINVAL, "getxattr(a.txt, user.)");
    expectEq(f.invoke(191, FixturePV64::aTxtAt, FixturePV64::emptyStrAt, buf, 256), -K_ERANGE, "getxattr(a.txt, \"\")");
    expectEq(f.invoke(191, FixturePV64::missingAt, FixturePV64::bogusAt, buf, 256), -K_ENOENT, "getxattr(missing, bogus.x): resolution precedes the namespace");
    expectEq(f.invoke(191, FixturePV64::lnAt, FixturePV64::bogusAt, buf, 256), -K_ENOTSUP, "getxattr(ln, bogus.x) follows");
    expectEq(f.invoke(192, FixturePV64::aTxtAt, FixturePV64::bogusAt, buf, 256), -K_ENOTSUP, "lgetxattr(a.txt, bogus.x)");
    expectEq(f.invoke(188, FixturePV64::aTxtAt, FixturePV64::emptyStrAt, FixturePV64::bogusAt, 1, 0), -K_ERANGE, "setxattr(a.txt, \"\"): the fetch verdict reaches the set forms");

    // The descriptor form follows the same rule after its own
    // verdicts; the name fetch precedes the descriptor verdict.
    KFileDescriptorPtr fileDesc;
    if (f.process->openFile(B("/"), B("/a.txt"), K_O_RDONLY, fileDesc) != 0 || !fileDesc) {
        testFail("xattr namespace test could not open its file");
        return;
    }
    const U64 fileFd = fileDesc->handle;
    expectEq(f.invoke(193, fileFd, FixturePV64::bogusAt, buf, 256), -K_ENOTSUP, "fgetxattr(fd, bogus.x)");
    expectEq(f.invoke(193, fileFd, FixturePV64::nameUserAt, buf, 256), -K_ENODATA, "fgetxattr(fd, user.x) control");
    expectEq(f.invoke(193, fileFd, FixturePV64::bareUserAt, buf, 256), -K_EINVAL, "fgetxattr(fd, user.)");
    expectEq(f.invoke(193, 9999, FixturePV64::emptyStrAt, buf, 256), -K_ERANGE, "fgetxattr(9999, \"\"): the fetch verdict precedes the descriptor");
    expectEq(f.invoke(193, 9999, FixturePV64::bogusAt, buf, 256), -K_EBADF, "fgetxattr(9999, bogus.x) control");
    f.process->close(fileDesc->handle);
}

void testMidDotDot64Syscalls() {
    FixturePV64 f;
    if (!f.ok) {
        testFail("mid-dotdot test could not stage its fixture");
        return;
    }
    const U64 buf = FixturePV64::bufAt;

    // FD-164, host-measured at the raw syscall level
    // (test-results/fd162-164/hostref-fd162-164.out): a ".." in
    // the middle of a path is stepped through -- a missing
    // predecessor answers -ENOENT, a non-directory predecessor
    // -ENOTDIR -- and a ".." at the root stays at the root.
    expectEq(f.invoke(137, FixturePV64::missMidAt, buf), -K_ENOENT, "statfs(missing/../a.txt)");
    expectEq(f.invoke(137, FixturePV64::fileMidAt, buf), -K_ENOTDIR, "statfs(a.txt/../d)");
    expectEq(f.invoke(137, FixturePV64::dMidAt, buf), 0, "statfs(d/../a.txt) control");
    expectEq(f.invoke(137, FixturePV64::ldMidAt, buf), 0, "statfs(ld/../a.txt) through a link to a directory");
    expectEq(f.invoke(137, FixturePV64::lnMidAt, buf), -K_ENOTDIR, "statfs(ln/../d) through a link to a file");
    expectEq(f.invoke(137, FixturePV64::innerMidAt, buf), -K_ENOTDIR, "statfs(d/inner/../../a.txt)");
    expectEq(f.invoke(137, FixturePV64::deepMidAt, buf), 0, "statfs(d/../../a.txt) clamps at the root");
    expectEq(f.invoke(137, FixturePV64::dotdotOnlyAt, buf), 0, "statfs(..) at the root");
    expectEq(f.invoke(137, FixturePV64::absRootMidAt, buf), 0, "statfs(/../a.txt)");
    expectEq(f.invoke(137, FixturePV64::relRootMidAt, buf), 0, "statfs(\"../a.txt\") from the root");
    expectEq(f.invoke(4, FixturePV64::missMidAt, buf), -K_ENOENT, "stat(missing/../a.txt)");
    expectEq(f.invoke(191, FixturePV64::missMidAt, FixturePV64::nameUserAt, buf, 256), -K_ENOENT, "getxattr(missing/../a.txt, user.x)");
    expectEq(f.invoke(191, FixturePV64::fileMidAt, FixturePV64::nameUserAt, buf, 256), -K_ENOTDIR, "getxattr(a.txt/../d, user.x)");
}

void testRmdir64Syscalls() {
    FixturePV64 f;
    if (!f.ok) {
        testFail("rmdir test could not stage its fixture");
        return;
    }
    // The filed shape: syscall 84 had no case at the 64-bit
    // dispatcher and answered -ENOSYS for every path. The host
    // answers by shape (measured through unlinkat AT_REMOVEDIR,
    // the shape the kernel gives rmdir).
    expectEq(f.invoke(84, FixturePV64::missingAt), -K_ENOENT, "rmdir(missing)");
    expectEq(f.invoke(84, FixturePV64::aTxtAt), -K_ENOTDIR, "rmdir(a.txt) is a file");
    expectEq(f.invoke(84, FixturePV64::childAt), -K_ENOTDIR, "rmdir(a.txt/child)");
    expectEq(f.invoke(84, FixturePV64::dAt), -K_ENOTEMPTY, "rmdir(d) is not empty");
    expectEq(f.invoke(84, FixturePV64::emptyStrAt), -K_ENOENT, "rmdir(\"\")");
    expectEq(f.invoke(84, 0), -K_EFAULT, "rmdir(NULL)");
    expectEq(f.invoke(84, FixturePV64::emptyAt), 0, "rmdir(empty)");
    expectEq(f.invoke(84, FixturePV64::empty2TrailAt), 0, "rmdir(empty2/) trailing slash on a directory");
}

#endif
#endif
