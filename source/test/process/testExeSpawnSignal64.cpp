/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
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

#ifdef __TEST
#ifdef BOXEDWINE_GUEST_X64

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <iterator>
#include <unistd.h>

#include "../cpu/testCPU.h"
#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "ksystem.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"

// FD-54: the 64-bit process-spawn signal ("sys_openat64: OPENED '<path>'
// -> fd <n>" on a successful .exe open) is the line the launcher's
// session-readiness handshake matches to learn that the shell launched
// (FD-47/FD-48). sys_openat64 emitted it only when BW64_DLLTRACE was NOT
// set: with the diagnostic flag on, the same open produced only the
// DLLTRACE line, so a traced 64-bit session could never be certified --
// a diagnostic flag silently changed a contract signal's dialect. The
// 32-bit open paths emit the signal unconditionally (logExeSpawnSignal
// in syscall.cpp); the 64-bit path must too.
//
// The test drives the real dispatcher: it opens a fixture .exe through
// openat64 with klog captured (internal_log mirrors every line into
// KSystem::logFile when one is open), once without the flag (control:
// the line must be there) and once with BW64_DLLTRACE=1 (the line must
// STILL be there, alongside the DLLTRACE line that proves the trace
// path was active). A failed .exe open must never produce the line, in
// either configuration: the signal means a successful spawn, nothing
// more (FD-48).
void testExeSpawnSignal64() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x6d000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("x64 spawn-signal test could not map its fixture page");
        return;
    }
    constexpr U64 pathAt = page + 0x100;

    // Host fixture: a directory holding one real .exe file, installed as
    // the guest filesystem root so "/spawnprobe.exe" resolves to it.
    char dirTemplate[] = "/tmp/bw64-fd54-XXXXXX";
    const char* hostDir = mkdtemp(dirTemplate);
    if (!hostDir) {
        testFail("x64 spawn-signal test could not create its fixture directory");
        return;
    }
    const std::string hostDirStr = hostDir;
    {
        std::ofstream exe(hostDirStr + "/spawnprobe.exe", std::ios::binary);
        exe << "MZ spawn-signal fixture\n";
        if (!exe) {
            testFail("x64 spawn-signal test could not write its fixture .exe");
            return;
        }
    }
    struct FixtureCleanup {
        std::string dir;
        ~FixtureCleanup() {
            std::remove((dir + "/spawnprobe.exe").c_str());
            std::remove((dir + "/klog-capture.txt").c_str());
            rmdir(dir.c_str());
        }
    } fixtureCleanup{hostDirStr};

    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(hostDirStr.c_str()), true, true, nullptr);
    Fs::clearPathCache();

    struct EnvRestore {
        ~EnvRestore() { unsetenv("BW64_DLLTRACE"); }
    } envRestore;

    // klog capture: internal_log writes every line to KSystem::logFile
    // when it is open. The file also collects other suite threads' log
    // lines while open; the assertions below only search for this test's
    // own fixture path, which no other test emits.
    const std::string capturePath = hostDirStr + "/klog-capture.txt";
    auto captureOpen = [&]() -> bool {
        KSystem::logFile.close();
        return KSystem::logFile.createNew(BString::copy(capturePath.c_str()));
    };
    auto captureTake = [&]() -> std::string {
        KSystem::logFile.close();
        std::ifstream in(capturePath);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto openat = [&](const char* guestPath) -> S64 {
        memory->memcpyToGuest(pathAt, guestPath, strlen(guestPath) + 1);
        cpu.reg[X64_RAX].setU64(257); // __NR_openat
        cpu.reg[X64_RDI].setU64((U64)(S64)-100); // AT_FDCWD
        cpu.reg[X64_RSI].setU64(pathAt);
        cpu.reg[X64_RDX].setU64(0); // O_RDONLY
        cpu.reg[X64_R10].setU64(0);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto closeFd = [&](S64 fd) {
        cpu.reg[X64_RAX].setU64(3); // __NR_close
        cpu.reg[X64_RDI].setU64((U64)fd);
        ksyscall64(&cpu);
    };

    const std::string signal = "sys_openat64: OPENED '/spawnprobe.exe' -> fd ";

    // Control: no trace flag. The spawn signal must be emitted.
    unsetenv("BW64_DLLTRACE");
    if (!captureOpen()) {
        testFail("x64 spawn-signal test could not open its klog capture");
        return;
    }
    S64 fd = openat("/spawnprobe.exe");
    const std::string controlLog = captureTake();
    if (fd < 0) {
        testFail("x64 spawn-signal control open failed with %lld", (long long)fd);
        return;
    }
    closeFd(fd);
    if (controlLog.find(signal) == std::string::npos) {
        testFail("x64 spawn signal missing without BW64_DLLTRACE (control)");
    }

    // Traced: BW64_DLLTRACE=1. The DLLTRACE line must appear (the trace
    // path really was active) AND the spawn signal must still be
    // emitted in its default format.
    setenv("BW64_DLLTRACE", "1", 1);
    if (!captureOpen()) {
        testFail("x64 spawn-signal test could not reopen its klog capture");
        return;
    }
    fd = openat("/spawnprobe.exe");
    const std::string tracedLog = captureTake();
    if (fd < 0) {
        testFail("x64 spawn-signal traced open failed with %lld", (long long)fd);
        return;
    }
    closeFd(fd);
    if (tracedLog.find("DLLTRACE") == std::string::npos ||
        tracedLog.find("OPEN  '/spawnprobe.exe'") == std::string::npos) {
        testFail("x64 DLLTRACE open line missing with BW64_DLLTRACE=1 (trace path inactive?)");
    }
    if (tracedLog.find(signal) == std::string::npos) {
        testFail("FD-54: x64 spawn signal suppressed under BW64_DLLTRACE=1");
    }

    // Failure discipline, still traced: a failed .exe open must not
    // produce the spawn signal.
    if (!captureOpen()) {
        testFail("x64 spawn-signal test could not reopen its klog capture");
        return;
    }
    S64 badFd = openat("/missing.exe");
    const std::string failLog = captureTake();
    if (badFd >= 0) {
        closeFd(badFd);
        testFail("x64 spawn-signal test opened a nonexistent .exe (fd %lld)", (long long)badFd);
    }
    if (failLog.find("OPENED '/missing.exe'") != std::string::npos) {
        testFail("x64 spawn signal emitted for a FAILED .exe open under BW64_DLLTRACE=1");
    }
}

#endif // BOXEDWINE_GUEST_X64
#endif // __TEST
