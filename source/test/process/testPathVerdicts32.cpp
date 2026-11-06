#include "boxedwine.h"
#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifdef __TEST
#ifndef BOXEDWINE_GUEST_X64

#include "kerror.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

// Main-suite companions to testPathVerdicts64.cpp, for the
// register filings measured in
// test-results/fd157-161/hostref-fd157-161.c:
//   - the 32-bit extended-attribute calls resolved their target
//     only for the user.DOSATTRIB name and answered -ENOTSUP (or
//     -ENOSYS where no handler exists at all) for every other
//     name, resolved or not; the host resolves first and answers
//     -ENOENT for a missing path, -ENOTDIR through a file, and
//     -ENODATA for an attribute a resolved file does not have
//     (FD-157).
//   - readlink/readlinkat answered -EINVAL for a missing path on
//     the process path too (FD-158).
//   - the normalization-erased trailing shapes ("a.txt/",
//     "a.txt/.", "a.txt/..") at the 32-bit path calls (FD-159).
//   - a hard link whose destination is a bare filename in the
//     working directory answered -ENOENT even when the source
//     exists: the destination's parent was computed as the name
//     itself, so the parent lookup resolved the not-yet-existing
//     destination and missed (FD-161).
// Fixture tree: /a.txt (file), /d (directory) with /d/inner
// (file), /c.txt (file), /ln -> a.txt, /ld -> d. cwd is "/".

namespace {

struct RootSwapPV32 {
    std::shared_ptr<FsFileNode> previousRoot;
    std::string dir;
    bool ok = false;

    RootSwapPV32() {
        char dirTemplate[] = "/tmp/bw64pv32XXXXXX";
        char* d = mkdtemp(dirTemplate);
        if (!d) return;
        dir = d;
        previousRoot = Fs::rootNode;
        Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir.c_str()), true, true, nullptr);
        Fs::clearPathCache();
        ok = true;
    }
    ~RootSwapPV32() {
        if (previousRoot) {
            Fs::rootNode = previousRoot;
            Fs::clearPathCache();
        }
    }
};

bool stageTreePV32(KProcessPtr process, FD& dirFdOut) {
    KFileDescriptorPtr dirDesc;
    auto createFile = [&](BString path) -> bool {
        KFileDescriptorPtr desc;
        if (process->openFile(B("/"), path, K_O_RDWR | K_O_CREAT, desc) != 0 || !desc) return false;
        process->close(desc->handle);
        return true;
    };
    if (process->mkdir(B("/d")) != 0 ||
        !createFile(B("/a.txt")) || !createFile(B("/d/inner")) || !createFile(B("/c.txt")) ||
        process->symlink(B("a.txt"), B("/ln")) != 0 ||
        process->symlink(B("d"), B("/ld")) != 0 ||
        process->chdir(B("/")) != 0) {
        return false;
    }
    if (process->openFile(B("/"), B("/d"), K_O_RDONLY, dirDesc) != 0 || !dirDesc) return false;
    dirFdOut = dirDesc->handle;
    return true;
}

// Guest string slots on the test heap. stage() rewrites them
// inside invoke() because testNewInstruction() resets the heap
// (the testNotDir32Syscalls pattern).
constexpr U32 aTxtAt       = TEST_HEAP_ADDRESS + 0x000; // "a.txt"
constexpr U32 absAtxtAt    = TEST_HEAP_ADDRESS + 0x040; // "/a.txt"
constexpr U32 dAt          = TEST_HEAP_ADDRESS + 0x080; // "d"
constexpr U32 lnAt         = TEST_HEAP_ADDRESS + 0x0C0; // "ln"
constexpr U32 missingAt    = TEST_HEAP_ADDRESS + 0x100; // "missing"
constexpr U32 childAt      = TEST_HEAP_ADDRESS + 0x140; // "a.txt/child"
constexpr U32 missingChild = TEST_HEAP_ADDRESS + 0x180; // "missing/child"
constexpr U32 emptyAt      = TEST_HEAP_ADDRESS + 0x1C0; // ""
constexpr U32 nameAt       = TEST_HEAP_ADDRESS + 0x200; // "user.x"
constexpr U32 dosAt        = TEST_HEAP_ADDRESS + 0x240; // "user.DOSATTRIB"
constexpr U32 valAt        = TEST_HEAP_ADDRESS + 0x280; // "v"
constexpr U32 trailAt      = TEST_HEAP_ADDRESS + 0x2C0; // "a.txt/"
constexpr U32 dotAt        = TEST_HEAP_ADDRESS + 0x300; // "a.txt/."
constexpr U32 dotdotAt     = TEST_HEAP_ADDRESS + 0x340; // "a.txt/.."
constexpr U32 dTrailAt     = TEST_HEAP_ADDRESS + 0x380; // "d/"
constexpr U32 dDotAt       = TEST_HEAP_ADDRESS + 0x3C0; // "d/."
constexpr U32 dDotdotAt    = TEST_HEAP_ADDRESS + 0x400; // "d/.."
constexpr U32 missTrailAt  = TEST_HEAP_ADDRESS + 0x440; // "missing/"
constexpr U32 missDotdotAt = TEST_HEAP_ADDRESS + 0x480; // "missing/.."
constexpr U32 missMidAt    = TEST_HEAP_ADDRESS + 0x4C0; // "missing/../a.txt"
constexpr U32 fileMidAt    = TEST_HEAP_ADDRESS + 0x500; // "a.txt/../d"
constexpr U32 innerAt      = TEST_HEAP_ADDRESS + 0x540; // "inner"
constexpr U32 hlAt         = TEST_HEAP_ADDRESS + 0x580; // "hl.txt"
constexpr U32 ldTrailAt    = TEST_HEAP_ADDRESS + 0x5C0; // "ld/"
constexpr U32 bufAt        = TEST_HEAP_ADDRESS + 0x600; // out buffers
constexpr U32 lnTrailAt    = TEST_HEAP_ADDRESS + 0x700; // "ln/"
constexpr U32 bogusAt      = TEST_HEAP_ADDRESS + 0x800; // "bogus.x"
constexpr U32 trustedAt    = TEST_HEAP_ADDRESS + 0x840; // "trusted.x"
constexpr U32 securityAt   = TEST_HEAP_ADDRESS + 0x880; // "security.x"
constexpr U32 bareUserAt   = TEST_HEAP_ADDRESS + 0x8C0; // "user."
constexpr U32 aclAt        = TEST_HEAP_ADDRESS + 0x900; // "system.posix_acl_access"
constexpr U32 systemAt     = TEST_HEAP_ADDRESS + 0x980; // "system.x"
constexpr U32 dMidAt       = TEST_HEAP_ADDRESS + 0x9C0; // "d/../a.txt"
constexpr U32 ldMidAt      = TEST_HEAP_ADDRESS + 0xA00; // "ld/../a.txt"
constexpr U32 lnMidAt      = TEST_HEAP_ADDRESS + 0xA40; // "ln/../d"
constexpr U32 innerMidAt   = TEST_HEAP_ADDRESS + 0xA80; // "d/inner/../../a.txt"
constexpr U32 deepMidAt    = TEST_HEAP_ADDRESS + 0xAC0; // "d/../../a.txt"
constexpr U32 dotdotOnly32 = TEST_HEAP_ADDRESS + 0xB00; // ".."
constexpr U32 absRootMidAt = TEST_HEAP_ADDRESS + 0xB40; // "/../a.txt"
constexpr U32 relRootMidAt = TEST_HEAP_ADDRESS + 0xB80; // "../a.txt"
constexpr U32 bareTrustedAt = TEST_HEAP_ADDRESS + 0xBC0; // "trusted."
constexpr U32 bareSecurityAt = TEST_HEAP_ADDRESS + 0xC00; // "security."
constexpr U32 bareSystemAt  = TEST_HEAP_ADDRESS + 0xC40; // "system."
constexpr U32 bareWordAt    = TEST_HEAP_ADDRESS + 0xC80; // "bogus"
constexpr U32 userYAt       = TEST_HEAP_ADDRESS + 0xCC0; // "user.y"
constexpr U32 userZAt       = TEST_HEAP_ADDRESS + 0xD00; // "user.z"
constexpr U32 aclDefaultAt  = TEST_HEAP_ADDRESS + 0xD40; // "system.posix_acl_default"
constexpr U32 cTxtAt        = TEST_HEAP_ADDRESS + 0xD80; // "c.txt"

void stageStringsPV32(KMemory* memory) {
    memory->memcpy(aTxtAt, "a.txt", 6);
    memory->memcpy(absAtxtAt, "/a.txt", 7);
    memory->memcpy(dAt, "d", 2);
    memory->memcpy(lnAt, "ln", 3);
    memory->memcpy(missingAt, "missing", 8);
    memory->memcpy(childAt, "a.txt/child", 12);
    memory->memcpy(missingChild, "missing/child", 14);
    memory->memcpy(emptyAt, "", 1);
    memory->memcpy(nameAt, "user.x", 7);
    memory->memcpy(dosAt, "user.DOSATTRIB", 15);
    memory->memcpy(valAt, "v", 2);
    memory->memcpy(trailAt, "a.txt/", 7);
    memory->memcpy(dotAt, "a.txt/.", 8);
    memory->memcpy(dotdotAt, "a.txt/..", 9);
    memory->memcpy(dTrailAt, "d/", 3);
    memory->memcpy(dDotAt, "d/.", 4);
    memory->memcpy(dDotdotAt, "d/..", 5);
    memory->memcpy(missTrailAt, "missing/", 9);
    memory->memcpy(missDotdotAt, "missing/..", 11);
    memory->memcpy(missMidAt, "missing/../a.txt", 17);
    memory->memcpy(fileMidAt, "a.txt/../d", 11);
    memory->memcpy(innerAt, "inner", 6);
    memory->memcpy(hlAt, "hl.txt", 7);
    memory->memcpy(ldTrailAt, "ld/", 4);
    memory->memcpy(lnTrailAt, "ln/", 4);
    memory->memcpy(bogusAt, "bogus.x", 8);
    memory->memcpy(trustedAt, "trusted.x", 10);
    memory->memcpy(securityAt, "security.x", 11);
    memory->memcpy(bareUserAt, "user.", 6);
    memory->memcpy(aclAt, "system.posix_acl_access", 24);
    memory->memcpy(systemAt, "system.x", 9);
    memory->memcpy(dMidAt, "d/../a.txt", 11);
    memory->memcpy(ldMidAt, "ld/../a.txt", 12);
    memory->memcpy(lnMidAt, "ln/../d", 8);
    memory->memcpy(innerMidAt, "d/inner/../../a.txt", 20);
    memory->memcpy(deepMidAt, "d/../../a.txt", 14);
    memory->memcpy(dotdotOnly32, "..", 3);
    memory->memcpy(absRootMidAt, "/../a.txt", 10);
    memory->memcpy(relRootMidAt, "../a.txt", 9);
    memory->memcpy(bareTrustedAt, "trusted.", 9);
    memory->memcpy(bareSecurityAt, "security.", 10);
    memory->memcpy(bareSystemAt, "system.", 8);
    memory->memcpy(bareWordAt, "bogus", 6);
    memory->memcpy(userYAt, "user.y", 7);
    memory->memcpy(userZAt, "user.z", 7);
    memory->memcpy(aclDefaultAt, "system.posix_acl_default", 25);
    memory->memcpy(cTxtAt, "c.txt", 6);
}

struct InvokePV32 {
    TestContext& context;
    CPU* cpu;
    explicit InvokePV32(TestContext& c) : context(c), cpu(c.cpu) {}
    S32 operator()(U32 nr, U32 a1, U32 a2, U32 a3, U32 a4, U32 a5 = 0) {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(nr); // mov eax, nr
        testPushCode8(0xCD); testPushCode8(0x80); // int 0x80
        stageStringsPV32(context.memory);
        cpu->reg[3].u32 = a1; // ebx
        cpu->reg[1].u32 = a2; // ecx
        cpu->reg[2].u32 = a3; // edx
        cpu->reg[6].u32 = a4; // esi
        cpu->reg[7].u32 = a5; // edi
        testRunCPU();
        return (S32)cpu->reg[0].u32;
    }
};

void expectEq32(const char* what, S32 got, S32 want) {
    if (got != want) testFail("%s returned %d (expected %d)", what, got, want);
}

} // namespace

void testXattrPathResolve32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    RootSwapPV32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTreePV32(process, dirFd)) {
        testFail("xattr resolve test could not stage its fixture");
        return;
    }
    InvokePV32 invoke(context);

    // getxattr (229) / lgetxattr (230): the target resolves before
    // the attribute verdict.
    expectEq32("getxattr(missing, user.x)", invoke(229, missingAt, nameAt, bufAt, 256), -K_ENOENT);
    expectEq32("getxattr(a.txt, user.x)", invoke(229, aTxtAt, nameAt, bufAt, 256), -K_ENODATA);
    expectEq32("getxattr(d, user.x)", invoke(229, dAt, nameAt, bufAt, 256), -K_ENODATA);
    expectEq32("getxattr(a.txt/child, user.x)", invoke(229, childAt, nameAt, bufAt, 256), -K_ENOTDIR);
    expectEq32("getxattr(missing/child, user.x)", invoke(229, missingChild, nameAt, bufAt, 256), -K_ENOENT);
    expectEq32("getxattr(ln, user.x) follows", invoke(229, lnAt, nameAt, bufAt, 256), -K_ENODATA);
    expectEq32("lgetxattr(missing, user.x)", invoke(230, missingAt, nameAt, bufAt, 256), -K_ENOENT);
    expectEq32("lgetxattr(a.txt, user.x)", invoke(230, aTxtAt, nameAt, bufAt, 256), -K_ENODATA);
    expectEq32("lgetxattr(ln, user.x) the link itself", invoke(230, lnAt, nameAt, bufAt, 256), -K_ENODATA);

    // Entry order, host-measured: a null path faults; a null name
    // faults ahead of the path verdicts (even an empty path's);
    // an empty path with a valid name answers -ENOENT.
    expectEq32("getxattr(NULL, user.x)", invoke(229, 0, nameAt, bufAt, 256), -K_EFAULT);
    expectEq32("getxattr(a.txt, NULL)", invoke(229, aTxtAt, 0, bufAt, 256), -K_EFAULT);
    expectEq32("getxattr(missing, NULL): name precedes path", invoke(229, missingAt, 0, bufAt, 256), -K_EFAULT);
    expectEq32("getxattr(\"\", NULL): name precedes the empty-path verdict", invoke(229, emptyAt, 0, bufAt, 256), -K_EFAULT);
    expectEq32("getxattr(\"\", user.x)", invoke(229, emptyAt, nameAt, bufAt, 256), -K_ENOENT);

    // setxattr (226) / lsetxattr (227): flags validate first, then
    // the name faults, then the target resolves. FD-165: a
    // recognised name on a resolved target is stored (the host
    // answers 0); the full classification is pinned by
    // testXattrWriteClassify32Syscalls.
    expectEq32("setxattr(missing, user.x, 0)", invoke(226, missingAt, nameAt, valAt, 1, 0), -K_ENOENT);
    expectEq32("setxattr(a.txt, user.x, 0)", invoke(226, aTxtAt, nameAt, valAt, 1, 0), 0);
    expectEq32("setxattr(a.txt/child, user.x, 0)", invoke(226, childAt, nameAt, valAt, 1, 0), -K_ENOTDIR);
    expectEq32("setxattr(missing, user.x, bad flags): flags precede path", invoke(226, missingAt, nameAt, valAt, 1, 0x10), -K_EINVAL);
    expectEq32("setxattr(a.txt, NULL, bad flags): flags precede name", invoke(226, aTxtAt, 0, valAt, 1, 0x10), -K_EINVAL);
    expectEq32("setxattr(a.txt, NULL, 0)", invoke(226, aTxtAt, 0, valAt, 1, 0), -K_EFAULT);
    expectEq32("setxattr(NULL, user.x, 0)", invoke(226, 0, nameAt, valAt, 1, 0), -K_EFAULT);
    expectEq32("lsetxattr(missing, user.x, 0)", invoke(227, missingAt, nameAt, valAt, 1, 0), -K_ENOENT);
    expectEq32("lsetxattr(a.txt, user.x, 0)", invoke(227, aTxtAt, nameAt, valAt, 1, 0), 0);

    // listxattr (232) / llistxattr (233): no name; the target
    // resolves, and a target with nothing stored has an empty
    // list (c.txt: the sets above stored user.x on a.txt).
    expectEq32("listxattr(missing)", invoke(232, missingAt, bufAt, 256, 0), -K_ENOENT);
    expectEq32("listxattr(c.txt)", invoke(232, cTxtAt, bufAt, 256, 0), 0);
    expectEq32("listxattr(a.txt/child)", invoke(232, childAt, bufAt, 256, 0), -K_ENOTDIR);
    expectEq32("listxattr(NULL)", invoke(232, 0, bufAt, 256, 0), -K_EFAULT);
    expectEq32("llistxattr(missing)", invoke(233, missingAt, bufAt, 256, 0), -K_ENOENT);
    expectEq32("llistxattr(c.txt)", invoke(233, cTxtAt, bufAt, 256, 0), 0);

    // removexattr (235) / lremovexattr (236): like the set forms
    // without the flags word; user.x was stored above, so its
    // removal succeeds.
    expectEq32("removexattr(missing, user.x)", invoke(235, missingAt, nameAt, 0, 0), -K_ENOENT);
    expectEq32("removexattr(a.txt, user.x)", invoke(235, aTxtAt, nameAt, 0, 0), 0);
    expectEq32("removexattr(a.txt/child, user.x)", invoke(235, childAt, nameAt, 0, 0), -K_ENOTDIR);
    expectEq32("removexattr(a.txt, NULL)", invoke(235, aTxtAt, 0, 0, 0), -K_EFAULT);
    expectEq32("lremovexattr(missing, user.x)", invoke(236, missingAt, nameAt, 0, 0), -K_ENOENT);

    // The DOSATTRIB special case is preserved exactly.
    expectEq32("getxattr(a.txt, user.DOSATTRIB) control", invoke(229, aTxtAt, dosAt, bufAt, 64), -K_ENODATA);
    expectEq32("getxattr(missing, user.DOSATTRIB) control", invoke(229, missingAt, dosAt, bufAt, 64), -K_ENOENT);
    expectEq32("getxattr(a.txt/child, user.DOSATTRIB) control", invoke(229, childAt, dosAt, bufAt, 64), -K_ENOTDIR);
}

void testReadlinkMissing32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    RootSwapPV32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTreePV32(process, dirFd)) {
        testFail("readlink test could not stage its fixture");
        return;
    }
    InvokePV32 invoke(context);
    constexpr U32 atFdcwd = (U32)-100;

    // The filed shape: a missing path answers -ENOENT, not the
    // -EINVAL a resolved non-link answers.
    expectEq32("readlink(missing)", invoke(85, missingAt, bufAt, 256, 0), -K_ENOENT);
    expectEq32("readlink(missing/child)", invoke(85, missingChild, bufAt, 256, 0), -K_ENOENT);
    expectEq32("readlinkat(AT_FDCWD, missing)", invoke(305, atFdcwd, missingAt, bufAt, 256), -K_ENOENT);
    expectEq32("readlinkat(dirfd, missing)", invoke(305, dirFd, missingAt, bufAt, 256), -K_ENOENT);

    // Controls.
    expectEq32("readlink(a.txt) control", invoke(85, aTxtAt, bufAt, 256, 0), -K_EINVAL);
    expectEq32("readlink(d) control", invoke(85, dAt, bufAt, 256, 0), -K_EINVAL);
    expectEq32("readlink(a.txt/child) control", invoke(85, childAt, bufAt, 256, 0), -K_ENOTDIR);
    expectEq32("readlinkat(dirfd, inner) control", invoke(305, dirFd, innerAt, bufAt, 256), -K_EINVAL);
    // FD-159: a trailing marker steps through the link and judges
    // its target (host-measured: readlink("ld/") -EINVAL,
    // readlink("ln/") -ENOTDIR).
    expectEq32("readlink(ld/): the target is a directory, not a link", invoke(85, ldTrailAt, bufAt, 256, 0), -K_EINVAL);
    expectEq32("readlink(ln/): the marker steps into a regular file", invoke(85, lnTrailAt, bufAt, 256, 0), -K_ENOTDIR);
    S32 len = invoke(85, lnAt, bufAt, 256, 0);
    expectEq32("readlink(ln) length control", len, 5);
    if (len == 5) {
        std::string target;
        for (U32 i = 0; i < 5; i++) target += (char)context.memory->readb(bufAt + i);
        if (target != "a.txt") testFail("readlink(ln) target is not a.txt");
    }
}

void testTrailingNotDir32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    RootSwapPV32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTreePV32(process, dirFd)) {
        testFail("trailing-shape test could not stage its fixture");
        return;
    }
    InvokePV32 invoke(context);

    // The filed shapes at the 32-bit path calls.
    expectEq32("statfs(a.txt/)", invoke(99, trailAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq32("statfs(a.txt/.)", invoke(99, dotAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq32("statfs(a.txt/..)", invoke(99, dotdotAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq32("stat64(a.txt/)", invoke(195, trailAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq32("stat64(a.txt/..)", invoke(195, dotdotAt, bufAt, 0, 0), -K_ENOTDIR);

    // Controls: the markers through a real directory resolve, and
    // a missing anchor keeps -ENOENT on every spelling.
    expectEq32("statfs(d/) control", invoke(99, dTrailAt, bufAt, 0, 0), 0);
    expectEq32("statfs(d/.) control", invoke(99, dDotAt, bufAt, 0, 0), 0);
    expectEq32("statfs(d/..) control", invoke(99, dDotdotAt, bufAt, 0, 0), 0);
    expectEq32("statfs(missing/) control", invoke(99, missTrailAt, bufAt, 0, 0), -K_ENOENT);
    expectEq32("statfs(missing/..)", invoke(99, missDotdotAt, bufAt, 0, 0), -K_ENOENT);

    // The same verdicts through the process API.
    expectEq32("process stat64(a.txt/)", (S32)process->stat64(B("a.txt/"), bufAt), -K_ENOTDIR);
    expectEq32("process stat64(a.txt/.)", (S32)process->stat64(B("a.txt/."), bufAt), -K_ENOTDIR);
    expectEq32("process stat64(a.txt/..)", (S32)process->stat64(B("a.txt/.."), bufAt), -K_ENOTDIR);
    expectEq32("process stat64(ld/) control", (S32)process->stat64(B("ld/"), bufAt), 0);
    expectEq32("process stat64(d/..) control", (S32)process->stat64(B("d/.."), bufAt), 0);
    // rmdir of a file spelled with a trailing slash answers
    // -ENOTDIR by either route; pinned as a control.
    expectEq32("process rmdir(a.txt/) control", (S32)process->rmdir(B("a.txt/")), -K_ENOTDIR);

    // The mid-path erased ".." pair these shapes used to print
    // as observations is pinned by testMidDotDot32Syscalls.
}

void testLinkBareDestination32() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    RootSwapPV32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTreePV32(process, dirFd)) {
        testFail("link test could not stage its fixture");
        return;
    }
    InvokePV32 invoke(context);

    // The filed shape: a bare-filename destination in the working
    // directory. The host creates the link (same inode, link
    // count 2).
    expectEq32("link(a.txt, hl.txt)", (S32)process->link(B("a.txt"), B("hl.txt")), 0);
    struct stat s1, s2;
    std::string p1 = root.dir + "/a.txt";
    std::string p2 = root.dir + "/hl.txt";
    if (::stat(p1.c_str(), &s1) != 0 || ::stat(p2.c_str(), &s2) != 0) {
        testFail("link(a.txt, hl.txt) did not create the native destination");
    } else if (s1.st_ino != s2.st_ino || s2.st_nlink != 2) {
        testFail("link(a.txt, hl.txt) native files are not one inode with count 2");
    }
    expectEq32("stat64(hl.txt) sees the new name", (S32)process->stat64(B("hl.txt"), bufAt), 0);

    // Controls.
    expectEq32("link(a.txt, hl.txt) again", (S32)process->link(B("a.txt"), B("hl.txt")), -K_EEXIST);
    expectEq32("link(missing, hl2.txt) control", (S32)process->link(B("missing"), B("hl2.txt")), -K_ENOENT);
    expectEq32("link(a.txt, missing/hl.txt) control", (S32)process->link(B("a.txt"), B("missing/hl.txt")), -K_ENOENT);
    expectEq32("link(a.txt, a.txt/child) control", (S32)process->link(B("a.txt"), B("a.txt/child")), -K_ENOTDIR);
    expectEq32("link(d, hl3.txt) control: a directory source", (S32)process->link(B("d"), B("hl3.txt")), -K_EPERM);
    expectEq32("link(a.txt, d/hl.txt) control", (S32)process->link(B("a.txt"), B("d/hl.txt")), 0);

    // The dispatcher form of the same call.
    expectEq32("syscall link(a.txt, hl.txt)", invoke(9, absAtxtAt, hlAt, 0, 0), -K_EEXIST);
    expectEq32("rename(c.txt, c2.txt) bare-name audit control", (S32)process->rename(B("c.txt"), B("c2.txt")), 0);
    expectEq32("symlink(a.txt, sl2) bare-name audit control", (S32)process->symlink(B("a.txt"), B("sl2")), 0);
}

void testXattrDescriptor32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    RootSwapPV32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTreePV32(process, dirFd)) {
        testFail("xattr descriptor test could not stage its fixture");
        return;
    }
    KFileDescriptorPtr fileDesc;
    if (process->openFile(B("/"), B("/a.txt"), K_O_RDONLY, fileDesc) != 0 || !fileDesc) {
        testFail("xattr descriptor test could not open its file");
        return;
    }
    const U32 fileFd = fileDesc->handle;
    const U32 badFd = 9999;
    InvokePV32 invoke(context);

    // FD-162, host-measured at the raw syscall level
    // (test-results/fd162-164/hostref-fd162-164.out): the four
    // descriptor forms answer -EBADF for an unopened descriptor --
    // the host has no -EBADFD -- in the family's entry order: the
    // set form's flags word first (-EINVAL), then the name fetch
    // (a null name -EFAULT, an empty name -ERANGE), then the
    // descriptor verdict.
    expectEq32("fgetxattr(9999, user.x)", invoke(231, badFd, nameAt, bufAt, 256), -K_EBADF);
    expectEq32("fgetxattr(9999, bogus.x)", invoke(231, badFd, bogusAt, bufAt, 256), -K_EBADF);
    expectEq32("fgetxattr(9999, NULL)", invoke(231, badFd, 0, bufAt, 256), -K_EFAULT);
    expectEq32("fgetxattr(9999, \"\")", invoke(231, badFd, emptyAt, bufAt, 256), -K_ERANGE);
    expectEq32("fsetxattr(9999, user.x, 0)", invoke(228, badFd, nameAt, valAt, 1, 0), -K_EBADF);
    expectEq32("fsetxattr(9999, user.x, bad flags): flags precede fd", invoke(228, badFd, nameAt, valAt, 1, 0x10), -K_EINVAL);
    expectEq32("fsetxattr(9999, NULL, 0)", invoke(228, badFd, 0, valAt, 1, 0), -K_EFAULT);
    expectEq32("flistxattr(9999)", invoke(234, badFd, bufAt, 256, 0), -K_EBADF);
    expectEq32("fremovexattr(9999, user.x)", invoke(237, badFd, nameAt, 0, 0), -K_EBADF);
    expectEq32("fremovexattr(9999, NULL)", invoke(237, badFd, 0, 0, 0), -K_EFAULT);

    // FD-162: flistxattr consults its descriptor; a valid one has
    // an empty list.
    expectEq32("flistxattr(fileFd)", invoke(234, fileFd, bufAt, 256, 0), 0);
    expectEq32("flistxattr(dirFd)", invoke(234, dirFd, bufAt, 256, 0), 0);

    // Resolved answers on a valid descriptor: the get form
    // follows the FD-163 namespace rule. FD-165: the set form
    // stores a recognised name (the host answers 0) and the
    // remove form removes it; the full write-side classification
    // is pinned by testXattrWriteClassify32Syscalls.
    expectEq32("fgetxattr(fileFd, user.x)", invoke(231, fileFd, nameAt, bufAt, 256), -K_ENODATA);
    expectEq32("fgetxattr(fileFd, bogus.x)", invoke(231, fileFd, bogusAt, bufAt, 256), -K_ENOTSUP);
    expectEq32("fgetxattr(fileFd, user.)", invoke(231, fileFd, bareUserAt, bufAt, 256), -K_EINVAL);
    expectEq32("fgetxattr(fileFd, \"\")", invoke(231, fileFd, emptyAt, bufAt, 256), -K_ERANGE);
    expectEq32("fgetxattr(fileFd, user.DOSATTRIB) control", invoke(231, fileFd, dosAt, bufAt, 64), -K_ENODATA);
    expectEq32("fgetxattr(dirFd, user.x)", invoke(231, dirFd, nameAt, bufAt, 256), -K_ENODATA);
    expectEq32("fsetxattr(fileFd, user.x, 0) control", invoke(228, fileFd, nameAt, valAt, 1, 0), 0);
    expectEq32("fremovexattr(fileFd, user.x) control", invoke(237, fileFd, nameAt, 0, 0), 0);

    process->close(fileDesc->handle);
}

void testXattrNamespace32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    RootSwapPV32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTreePV32(process, dirFd)) {
        testFail("xattr namespace test could not stage its fixture");
        return;
    }
    InvokePV32 invoke(context);

    // FD-163, host-measured at the raw syscall level
    // (test-results/fd162-164/hostref-fd162-164.out and
    // hostref-fd163b/c/d.out): on a resolved target the kernel
    // looks the name's namespace up before the attribute. A
    // prefix it has no handler for (bogus.*, a bare word,
    // system.* other than the two posix_acl names) answers
    // -ENOTSUP; a recognised namespace (user.*, trusted.*,
    // security.*, system.posix_acl_access/_default) with an
    // absent attribute answers -ENODATA; a recognised prefix
    // with an empty attribute name answers -EINVAL; an empty
    // name answers -ERANGE at fetch time, ahead of every path
    // and descriptor verdict.
    expectEq32("getxattr(a.txt, bogus.x)", invoke(229, aTxtAt, bogusAt, bufAt, 256), -K_ENOTSUP);
    expectEq32("getxattr(a.txt, trusted.x)", invoke(229, aTxtAt, trustedAt, bufAt, 256), -K_ENODATA);
    expectEq32("getxattr(a.txt, security.x)", invoke(229, aTxtAt, securityAt, bufAt, 256), -K_ENODATA);
    expectEq32("getxattr(a.txt, system.x)", invoke(229, aTxtAt, systemAt, bufAt, 256), -K_ENOTSUP);
    expectEq32("getxattr(a.txt, system.posix_acl_access)", invoke(229, aTxtAt, aclAt, bufAt, 256), -K_ENODATA);
    expectEq32("getxattr(a.txt, user.)", invoke(229, aTxtAt, bareUserAt, bufAt, 256), -K_EINVAL);
    expectEq32("getxattr(a.txt, \"\")", invoke(229, aTxtAt, emptyAt, bufAt, 256), -K_ERANGE);
    expectEq32("getxattr(NULL, \"\"): the name fetch precedes the path fault", invoke(229, 0, emptyAt, bufAt, 256), -K_ERANGE);
    expectEq32("getxattr(missing, bogus.x): resolution precedes the namespace", invoke(229, missingAt, bogusAt, bufAt, 256), -K_ENOENT);
    expectEq32("getxattr(a.txt/child, bogus.x)", invoke(229, childAt, bogusAt, bufAt, 256), -K_ENOTDIR);
    expectEq32("getxattr(d, bogus.x)", invoke(229, dAt, bogusAt, bufAt, 256), -K_ENOTSUP);
    expectEq32("getxattr(ln, bogus.x) follows", invoke(229, lnAt, bogusAt, bufAt, 256), -K_ENOTSUP);
    expectEq32("lgetxattr(a.txt, bogus.x)", invoke(230, aTxtAt, bogusAt, bufAt, 256), -K_ENOTSUP);
    expectEq32("lgetxattr(ln, bogus.x) the link itself", invoke(230, lnAt, bogusAt, bufAt, 256), -K_ENOTSUP);
    expectEq32("setxattr(a.txt, \"\", 0): the fetch verdict reaches the set forms", invoke(226, aTxtAt, emptyAt, valAt, 1, 0), -K_ERANGE);
    expectEq32("removexattr(a.txt, \"\")", invoke(235, aTxtAt, emptyAt, 0, 0), -K_ERANGE);
}

void testMidDotDot32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    RootSwapPV32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTreePV32(process, dirFd)) {
        testFail("mid-dotdot test could not stage its fixture");
        return;
    }
    InvokePV32 invoke(context);

    // FD-164, host-measured at the raw syscall level
    // (test-results/fd162-164/hostref-fd162-164.out): a ".." away
    // from the end of a path is stepped through, not erased with
    // its predecessor -- the predecessor must resolve (a miss
    // answers -ENOENT) and must be a directory (a file, or a
    // link to a file, answers -ENOTDIR). A ".." at the root
    // stays at the root.
    expectEq32("statfs(missing/../a.txt)", invoke(99, missMidAt, bufAt, 0, 0), -K_ENOENT);
    expectEq32("statfs(a.txt/../d)", invoke(99, fileMidAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq32("statfs(d/../a.txt) control", invoke(99, dMidAt, bufAt, 0, 0), 0);
    expectEq32("statfs(ld/../a.txt) through a link to a directory", invoke(99, ldMidAt, bufAt, 0, 0), 0);
    expectEq32("statfs(ln/../d) through a link to a file", invoke(99, lnMidAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq32("statfs(d/inner/../../a.txt)", invoke(99, innerMidAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq32("statfs(d/../../a.txt) clamps at the root", invoke(99, deepMidAt, bufAt, 0, 0), 0);
    expectEq32("statfs(..) at the root", invoke(99, dotdotOnly32, bufAt, 0, 0), 0);
    expectEq32("statfs(/../a.txt)", invoke(99, absRootMidAt, bufAt, 0, 0), 0);
    expectEq32("statfs(\"../a.txt\") from the root", invoke(99, relRootMidAt, bufAt, 0, 0), 0);
    expectEq32("stat64(missing/../a.txt)", invoke(195, missMidAt, bufAt, 0, 0), -K_ENOENT);
    expectEq32("stat64(a.txt/../d)", invoke(195, fileMidAt, bufAt, 0, 0), -K_ENOTDIR);
    expectEq32("getxattr(missing/../a.txt, user.x)", invoke(229, missMidAt, nameAt, bufAt, 256), -K_ENOENT);
    expectEq32("getxattr(a.txt/../d, user.x)", invoke(229, fileMidAt, nameAt, bufAt, 256), -K_ENOTDIR);
    expectEq32("process stat64(missing/../a.txt)", (S32)process->stat64(B("missing/../a.txt"), bufAt), -K_ENOENT);
    expectEq32("process stat64(a.txt/../d)", (S32)process->stat64(B("a.txt/../d"), bufAt), -K_ENOTDIR);
}

void testXattrWriteClassify32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    RootSwapPV32 root;
    FD dirFd = (FD)-1;
    if (!root.ok || !stageTreePV32(process, dirFd)) {
        testFail("xattr write-classify test could not stage its fixture");
        return;
    }
    KFileDescriptorPtr fileDesc;
    if (process->openFile(B("/"), B("/a.txt"), K_O_RDONLY, fileDesc) != 0 || !fileDesc) {
        testFail("xattr write-classify test could not open its file");
        return;
    }
    const U32 fileFd = fileDesc->handle;
    const U32 badFd = 9999;
    InvokePV32 invoke(context);

    // FD-165, host-measured at the raw syscall level
    // (test-results/fd165/hostref-fd165*.out): the set/remove
    // forms classify the name on a resolved target, the way the
    // get forms already did (FD-163). A prefix with no handler
    // (bogus.*, a bare word, system.* other than the two
    // posix_acl names) answers -ENOTSUP; a recognised prefix
    // with an empty attribute part answers -EINVAL; the two
    // posix_acl names answer -EINVAL to a set (the host
    // validates the value as an ACL and refuses a non-ACL
    // value) and a remove of them on a file or directory is a
    // no-op success. A recognised namespace with an attribute
    // part is stored: CREATE on a present attribute answers
    // -EEXIST, REPLACE on an absent one -ENODATA, and get/list
    // read the stored values back. On the link itself (the
    // l-forms) every user.* name answers -EPERM, while
    // trusted.*/security.* behave as on a file. The value copy
    // faults (-EFAULT for a null value with a nonzero size)
    // ahead of the target verdicts, after the flags word and
    // the name fetch.

    // Set-side classification on a resolved file.
    expectEq32("setxattr(a.txt, bogus.x) control", invoke(226, aTxtAt, bogusAt, valAt, 1, 0), -K_ENOTSUP);
    expectEq32("setxattr(a.txt, bogus) a bare word", invoke(226, aTxtAt, bareWordAt, valAt, 1, 0), -K_ENOTSUP);
    expectEq32("setxattr(a.txt, system.x)", invoke(226, aTxtAt, systemAt, valAt, 1, 0), -K_ENOTSUP);
    expectEq32("setxattr(a.txt, system.)", invoke(226, aTxtAt, bareSystemAt, valAt, 1, 0), -K_ENOTSUP);
    expectEq32("setxattr(a.txt, user.)", invoke(226, aTxtAt, bareUserAt, valAt, 1, 0), -K_EINVAL);
    expectEq32("setxattr(a.txt, trusted.)", invoke(226, aTxtAt, bareTrustedAt, valAt, 1, 0), -K_EINVAL);
    expectEq32("setxattr(a.txt, security.)", invoke(226, aTxtAt, bareSecurityAt, valAt, 1, 0), -K_EINVAL);
    expectEq32("setxattr(a.txt, system.posix_acl_access, v): the value is not an ACL", invoke(226, aTxtAt, aclAt, valAt, 1, 0), -K_EINVAL);
    expectEq32("setxattr(a.txt, system.posix_acl_default, v)", invoke(226, aTxtAt, aclDefaultAt, valAt, 1, 0), -K_EINVAL);
    expectEq32("setxattr(missing, user.): resolution precedes the classification", invoke(226, missingAt, bareUserAt, valAt, 1, 0), -K_ENOENT);
    expectEq32("setxattr(a.txt/child, user.x) control", invoke(226, childAt, nameAt, valAt, 1, 0), -K_ENOTDIR);
    // The value copy faults ahead of the target verdicts.
    expectEq32("setxattr(a.txt, user.x, NULL value, size 3)", invoke(226, aTxtAt, nameAt, 0, 3, 0), -K_EFAULT);
    expectEq32("setxattr(missing, user.x, NULL value, size 3): the value precedes the path", invoke(226, missingAt, nameAt, 0, 3, 0), -K_EFAULT);
    expectEq32("setxattr(a.txt, bogus.x, NULL value, size 3): the value precedes the classification", invoke(226, aTxtAt, bogusAt, 0, 3, 0), -K_EFAULT);

    // Storage: a recognised name is set, read back, listed, and
    // removed, with the host's CREATE/REPLACE verdicts.
    expectEq32("setxattr(a.txt, user.x)", invoke(226, aTxtAt, nameAt, valAt, 1, 0), 0);
    S32 gotLen = invoke(229, aTxtAt, nameAt, bufAt, 256);
    expectEq32("getxattr(a.txt, user.x) reads the set back", gotLen, 1);
    if (gotLen == 1 && context.memory->readb(bufAt) != 'v') testFail("getxattr(a.txt, user.x) value is not 'v'");
    expectEq32("getxattr(a.txt, user.x, size 0) is a size query", invoke(229, aTxtAt, nameAt, bufAt, 0), 1);
    expectEq32("setxattr(a.txt, user.x, CREATE) on a present attribute", invoke(226, aTxtAt, nameAt, valAt, 1, 1), -K_EEXIST);
    expectEq32("setxattr(a.txt, user.y, REPLACE) on an absent attribute", invoke(226, aTxtAt, userYAt, valAt, 1, 2), -K_ENODATA);
    expectEq32("setxattr(a.txt, user.y)", invoke(226, aTxtAt, userYAt, valAt, 1, 0), 0);
    expectEq32("setxattr(a.txt, user.y, REPLACE) on a present attribute", invoke(226, aTxtAt, userYAt, valAt, 1, 2), 0);
    S32 listLen = invoke(232, aTxtAt, bufAt, 256, 0);
    expectEq32("listxattr(a.txt) length", listLen, 14);
    if (listLen == 14) {
        const char* want = "user.x\0user.y\0";
        for (U32 i = 0; i < 14; i++) {
            if (context.memory->readb(bufAt + i) != (U8)want[i]) { testFail("listxattr(a.txt) bytes are not user.x/user.y"); break; }
        }
    }
    expectEq32("listxattr(a.txt, size 0) reports the needed size", invoke(232, aTxtAt, bufAt, 0, 0), 14);
    expectEq32("listxattr(a.txt, size 3) is too small", invoke(232, aTxtAt, bufAt, 3, 0), -K_ERANGE);
    expectEq32("removexattr(a.txt, user.x)", invoke(235, aTxtAt, nameAt, 0, 0), 0);
    expectEq32("removexattr(a.txt, user.x) again: absent", invoke(235, aTxtAt, nameAt, 0, 0), -K_ENODATA);
    expectEq32("getxattr(a.txt, user.x) is absent again", invoke(229, aTxtAt, nameAt, bufAt, 256), -K_ENODATA);

    // Remove-side classification on a resolved file (user.y is
    // still stored from the storage section).
    expectEq32("removexattr(a.txt, trusted.x) absent", invoke(235, aTxtAt, trustedAt, 0, 0), -K_ENODATA);
    expectEq32("removexattr(a.txt, security.x) absent", invoke(235, aTxtAt, securityAt, 0, 0), -K_ENODATA);
    expectEq32("removexattr(a.txt, bogus.x) control", invoke(235, aTxtAt, bogusAt, 0, 0), -K_ENOTSUP);
    expectEq32("removexattr(a.txt, user.)", invoke(235, aTxtAt, bareUserAt, 0, 0), -K_EINVAL);
    expectEq32("removexattr(a.txt, trusted.)", invoke(235, aTxtAt, bareTrustedAt, 0, 0), -K_EINVAL);
    expectEq32("removexattr(a.txt, security.)", invoke(235, aTxtAt, bareSecurityAt, 0, 0), -K_EINVAL);
    expectEq32("removexattr(a.txt, system.)", invoke(235, aTxtAt, bareSystemAt, 0, 0), -K_ENOTSUP);
    expectEq32("removexattr(a.txt, system.x)", invoke(235, aTxtAt, systemAt, 0, 0), -K_ENOTSUP);
    expectEq32("removexattr(a.txt, system.posix_acl_access): clearing an ACL is a no-op", invoke(235, aTxtAt, aclAt, 0, 0), 0);
    expectEq32("removexattr(a.txt, system.posix_acl_default): clearing an ACL is a no-op", invoke(235, aTxtAt, aclDefaultAt, 0, 0), 0);
    expectEq32("removexattr(missing, bogus.x): resolution precedes the classification", invoke(235, missingAt, bogusAt, 0, 0), -K_ENOENT);

    // The modelled attribute through the path forms (it used to
    // be reachable only through the descriptor forms).
    expectEq32("setxattr(a.txt, user.DOSATTRIB)", invoke(226, aTxtAt, dosAt, valAt, 1, 0), 0);
    S32 dosLen = invoke(229, aTxtAt, dosAt, bufAt, 64);
    expectEq32("getxattr(a.txt, user.DOSATTRIB) reads the set back", dosLen, 1);
    if (dosLen == 1 && context.memory->readb(bufAt) != 'v') testFail("getxattr(a.txt, user.DOSATTRIB) value is not 'v'");
    expectEq32("setxattr(a.txt, user.DOSATTRIB, CREATE) on a present attribute", invoke(226, aTxtAt, dosAt, valAt, 1, 1), -K_EEXIST);
    expectEq32("removexattr(a.txt, user.DOSATTRIB)", invoke(235, aTxtAt, dosAt, 0, 0), 0);
    expectEq32("removexattr(a.txt, user.DOSATTRIB) again: absent", invoke(235, aTxtAt, dosAt, 0, 0), -K_ENODATA);

    // The link itself (the l-forms): user.* is refused outright,
    // trusted.*/security.* behave as on a file.
    expectEq32("lsetxattr(ln, user.x): user.* never applies to a link", invoke(227, lnAt, nameAt, valAt, 1, 0), -K_EPERM);
    expectEq32("lsetxattr(ln, user.)", invoke(227, lnAt, bareUserAt, valAt, 1, 0), -K_EPERM);
    expectEq32("lsetxattr(ln, user.DOSATTRIB)", invoke(227, lnAt, dosAt, valAt, 1, 0), -K_EPERM);
    expectEq32("lsetxattr(ln, bogus.x)", invoke(227, lnAt, bogusAt, valAt, 1, 0), -K_ENOTSUP);
    expectEq32("lsetxattr(ln, trusted.)", invoke(227, lnAt, bareTrustedAt, valAt, 1, 0), -K_EINVAL);
    expectEq32("lsetxattr(ln, trusted.x) is stored on the link", invoke(227, lnAt, trustedAt, valAt, 1, 0), 0);
    expectEq32("lgetxattr(ln, trusted.x) reads it back", invoke(230, lnAt, trustedAt, bufAt, 256), 1);
    expectEq32("lremovexattr(ln, trusted.x)", invoke(236, lnAt, trustedAt, 0, 0), 0);
    expectEq32("lremovexattr(ln, trusted.x) again: absent", invoke(236, lnAt, trustedAt, 0, 0), -K_ENODATA);
    expectEq32("lremovexattr(ln, user.x)", invoke(236, lnAt, nameAt, 0, 0), -K_EPERM);
    expectEq32("lremovexattr(ln, system.posix_acl_access)", invoke(236, lnAt, aclAt, 0, 0), -K_ENOTSUP);
    // The following form lands on the target.
    expectEq32("setxattr(ln, user.x) follows to a.txt", invoke(226, lnAt, nameAt, valAt, 1, 0), 0);
    expectEq32("getxattr(a.txt, user.x) sees the followed set", invoke(229, aTxtAt, nameAt, bufAt, 256), 1);
    expectEq32("removexattr(a.txt, user.x) cleanup", invoke(235, aTxtAt, nameAt, 0, 0), 0);

    // A directory is a normal target.
    expectEq32("setxattr(d, user.x)", invoke(226, dAt, nameAt, valAt, 1, 0), 0);
    expectEq32("removexattr(d, user.x)", invoke(235, dAt, nameAt, 0, 0), 0);
    expectEq32("removexattr(d, system.posix_acl_access): the no-op clear applies to directories", invoke(235, dAt, aclAt, 0, 0), 0);
    expectEq32("setxattr(d, user.)", invoke(226, dAt, bareUserAt, valAt, 1, 0), -K_EINVAL);

    // The descriptor forms follow the same classification after
    // their own verdicts (user.y is still stored on a.txt).
    expectEq32("fsetxattr(fileFd, user.)", invoke(228, fileFd, bareUserAt, valAt, 1, 0), -K_EINVAL);
    expectEq32("fsetxattr(fileFd, system.posix_acl_access, v)", invoke(228, fileFd, aclAt, valAt, 1, 0), -K_EINVAL);
    expectEq32("fsetxattr(fileFd, user.x, NULL value, size 3)", invoke(228, fileFd, nameAt, 0, 3, 0), -K_EFAULT);
    expectEq32("fsetxattr(9999, user.x, NULL value, size 3): the value precedes the descriptor", invoke(228, badFd, nameAt, 0, 3, 0), -K_EFAULT);
    expectEq32("fsetxattr(fileFd, user.z)", invoke(228, fileFd, userZAt, valAt, 1, 0), 0);
    expectEq32("fgetxattr(fileFd, user.z) reads the set back", invoke(231, fileFd, userZAt, bufAt, 256), 1);
    expectEq32("flistxattr(fileFd) lists user.y and user.z", invoke(234, fileFd, bufAt, 256, 0), 14);
    expectEq32("fremovexattr(fileFd, user.DOSATTRIB): the modelled name on a file that does not carry it", invoke(237, fileFd, dosAt, 0, 0), -K_ENODATA);
    expectEq32("fremovexattr(fileFd, user.)", invoke(237, fileFd, bareUserAt, 0, 0), -K_EINVAL);
    expectEq32("fremovexattr(fileFd, bogus.x) control", invoke(237, fileFd, bogusAt, 0, 0), -K_ENOTSUP);
    expectEq32("fremovexattr(fileFd, system.posix_acl_access): the no-op clear", invoke(237, fileFd, aclAt, 0, 0), 0);
    expectEq32("fremovexattr(fileFd, user.z)", invoke(237, fileFd, userZAt, 0, 0), 0);
    expectEq32("fremovexattr(dirFd, user.x): a directory descriptor, absent", invoke(237, dirFd, nameAt, 0, 0), -K_ENODATA);

    process->close(fileDesc->handle);
}

#endif
#endif
