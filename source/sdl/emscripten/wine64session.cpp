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

// wine64session.cpp — "persistent wine session" for the browser (wasm64-mt).
//
// The roadmap item: load a new app into the SAME running wine, instead of
// reloading the page and re-booting wine64 per app. The pieces that make this
// possible already exist in Boxedwine:
//
//   * The emscripten main loop (source/sdl/emscripten/mainloop.cpp) runs via
//     emscripten_set_main_loop and only stops when SDL_QUIT is posted —
//     postQuit() does that when platformThreadCount hits 0 (the last guest
//     thread exited). So as long as ONE guest process stays alive, the kernel,
//     the X11 wire server, the GL context and the wine prefix all stay resident.
//
//   * KProcess::create()->startProcess(...) spawns a brand-new guest process
//     into the live kernel and scheduleThread()s it (platformThreadCount++),
//     exactly as the first app was launched in StartUpArgs::apply().
//
// So a persistent session is:
//   1. Boot wine64's wineserver as a long-lived FOREGROUND process
//      (`wineserver64 -f -p`): -f = don't fork/daemonize (the in-emulator
//      daemonize path is unreliable — see wine64-launcher's ?boot=1 note), -p =
//      persist forever. This is the process that keeps platformThreadCount > 0
//      between apps, so the loop never quits and the prefix stays warm.
//   2. Spawn each app as `wine64 <app>` against that running server, via
//      bw64_spawn() below, with NO page reload.
//
// THREADING. The spawn must run on the main-loop thread (it mutates kernel
// structures the loop also touches and calls KThread::setCurrentThread). JS calls
// the exported bw64_spawn() — under PROXY_TO_PTHREAD that runs on the BROWSER
// main thread, where Atomics.wait is forbidden, so we must NOT block there. So
// bw64_spawn just enqueues the request behind an atomic_flag spinlock (no OS
// wait) and returns; the main loop drains the queue each tick via
// bw64SessionDrainSpawns() and does the real startProcess there.

#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_MULTI_THREADED)

#include <emscripten/emscripten.h>
#include "boxedwine.h"
#include "kprocess.h"
#include "ksystem.h"
#include "kthread.h"
#include "ksignal.h" // K_SIGTERM
#include "../../io/fs.h"      // Fs::getNodeFromLocalPath / addFileNode (VFS registration)
#include "../../io/fsnode.h"
#include "../../x11wire/xwirepresent.h"
#include "kunixsocket.h" // xwireInjectKey (bw64_xwire_key), KUnixSocketObject
#include "ksocket.h" // K_AF_UNIX, K_SOCK_STREAM (doSignalWineboot)
#include "kobject.h" // KTYPE_UNIX_SOCKET (doSignalWineboot socket validation)

#include <atomic>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

// --- captured session context -------------------------------------------------
// StartUpArgs::apply() (the boot path) fills these once, so later spawns reuse
// the EXACT working dir / uids / env the first (booted) process ran with. Reusing
// the captured boot env is important: a spawned app must get the identical wine
// environment (HOME, WINEPREFIX, WINESERVER, WINEDLLPATH, PATH, USER, …) the boot
// app got, or wine inside it can't find the prefix / wineserver socket and hangs
// after connecting to X11 (observed: glcube stalled with only envc=6 vs the boot
// app's envc=16). So the bridge STARTS from g_sessionCtx.env for every spawn; an
// env passed from JS (bw64_spawn's envJoined) is merged OVER it per-key (O2) so
// diagnostic vars like WINEDEBUG can reach spawned guests — see mergeSpawnEnv.
struct Wine64SessionCtx {
    std::atomic<bool> valid{false};
    BString workingDir;
    std::vector<BString> env;
    int userId = 0;
    int groupId = 0;
    int effectiveUserId = 0;
    int effectiveGroupId = 0;
};
static Wine64SessionCtx g_sessionCtx;

// A queued in-session command: either spawn an app or kill a process. Both run
// on the main-loop thread, in order, so a "kill old app then spawn new app"
// switch is sequenced correctly.
struct Wine64Req {
    enum Kind { Spawn, Kill, RegisterFile, SignalWineboot } kind = Spawn;
    std::vector<BString> argv;   // Spawn
    std::vector<BString> env;    // Spawn
    U32 pid = 0;                 // Kill
    BString path;                // RegisterFile (guest local path, e.g. /home/username/foo.exe)
};

// Pending requests, guarded by an atomic_flag spinlock. A spinlock (busy CAS, no
// futex) is used deliberately: bw64_spawn()/bw64_kill() run on the browser main
// thread, where a blocking lock (Atomics.wait) is illegal. The critical section
// is tiny (a vector move), so the spin is negligible.
static std::vector<Wine64Req> g_pending;
static std::atomic_flag g_pendingLock = ATOMIC_FLAG_INIT;

// The pid of the most recently spawned app, published for JS so it can ask us to
// kill the previous app when the user switches (close-previous-on-switch).
static std::atomic<U32> g_lastSpawnedPid{0};

static void pendingLockAcquire() { while (g_pendingLock.test_and_set(std::memory_order_acquire)) {} }
static void pendingLockRelease() { g_pendingLock.clear(std::memory_order_release); }

// Called from StartUpArgs::apply() right before doMainLoop so the spawn bridge
// has the same launch parameters (incl. the FULL env) the boot process used.
void bw64SessionRememberContext(const BString& workingDir, const std::vector<BString>& env,
                                int userId, int groupId,
                                int effectiveUserId, int effectiveGroupId, U32 bootPid) {
    g_sessionCtx.workingDir = workingDir;
    g_sessionCtx.env = env;
    g_sessionCtx.userId = userId;
    g_sessionCtx.groupId = groupId;
    g_sessionCtx.effectiveUserId = effectiveUserId;
    g_sessionCtx.effectiveGroupId = effectiveGroupId;
    // Seed the "current app" pid with the boot app, so the FIRST switch closes
    // notepad (the booted app) and not nothing. Later bridge spawns update it.
    g_lastSpawnedPid.store(bootPid, std::memory_order_release);
    g_sessionCtx.valid.store(true, std::memory_order_release);
    klog_fmt("wine64session: context captured (cwd=%s uid=%d bootPid=%d)",
             workingDir.c_str(), userId, (int)bootPid);
}

// JS hands argv and env as '\n'-joined strings (newlines never appear in a
// program path or a wine env value we set, and this avoids quoting headaches).
static std::vector<BString> splitLines(const char* joined) {
    std::vector<BString> out;
    if (!joined) return out;
    std::string s(joined);
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string piece = (nl == std::string::npos) ? s.substr(start) : s.substr(start, nl - start);
        if (!piece.empty()) out.push_back(BString::copy(piece.c_str()));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

// Defined in source/x11wire/xwireserver.cpp (calls XWireServer::resetForAppSwitch);
// forward-declared here to avoid pulling the whole XWire header into this bridge.
void bw64ResetPresentForSwitch();
// Defined in source/x11wire/xwireserver.cpp: erase a killed app's host-side X
// presence (connections, windows, pixmaps) + wipe the canvas if it owned it.
void bw64DropAppPresenceByPid(uint32_t pid);
// Defined in source/x11wire/xwireserver.cpp: the guest pid that owns the window
// being switched away from (captured when the switch armed), one-shot consume.
uint32_t bw64TakeOutgoingAppPid();
// Defined in source/x11wire/xwireserver.cpp: clipboard bridge (M2). Set stages
// host text for guest paste (+ queues SelectionClear to the guest owner); get
// returns the latest harvested guest copy (pointer valid until the next call).
void bw64ClipboardSetHost(const char* text);
const char* bw64ClipboardGetHost();

// O2: build the env for an in-session spawn. Base is ALWAYS the captured boot
// env (full wine environment — see Wine64SessionCtx); every K=V the launcher
// passed in req.env then overrides (or adds to) that base, per key. An empty
// req.env returns the boot env unchanged, which is exactly the pre-O2 behavior,
// so this is opt-in and backward compatible. This is the channel that lets the
// launcher's prefixEnv() (?winedbg -> WINEDEBUG, BW64_* knobs) reach spawned
// guest processes instead of being silently dropped.
static std::vector<BString> mergeSpawnEnv(const std::vector<BString>& bootEnv, const std::vector<BString>& spawnEnv) {
    if (spawnEnv.empty()) return bootEnv;
    std::vector<BString> merged = bootEnv;
    for (const auto& kv : spawnEnv) {
        int eq = kv.indexOf('=');
        if (eq < 0) {
            // No '=' — not a K=V pair; keep boot env authoritative, append only
            // if the exact entry isn't already there.
            bool present = false;
            for (const auto& e : merged) {
                if (e == kv) { present = true; break; }
            }
            if (!present) merged.push_back(kv);
            continue;
        }
        BString prefix = kv.substr(0, eq + 1); // "KEY=" — compare with '=' so
        bool replaced = false;                 // HOME= never matches HOMEDRIVE=
        for (auto& e : merged) {
            if (e.startsWith(prefix.c_str())) {
                e = kv;
                replaced = true;
                break;
            }
        }
        if (!replaced) merged.push_back(kv);
    }
    return merged;
}

// The actual spawn — runs on the main-loop thread (see THREADING note).
static void doSpawn(const Wine64Req& req) {
    if (req.argv.empty()) {
        klog("wine64session: spawn with empty argv — ignoring");
        return;
    }
    klog_nonewline("wine64session: spawning ");
    for (auto& a : req.argv) klog_nonewline_fmt("\"%s\" ", a.c_str());
    klog_nonewline("\n");

    // startProcess() sets the calling thread's "current thread" to the new
    // process's first thread. On the main-loop thread that current-thread slot
    // is whatever the previous spawn/boot left; save and restore it so the loop
    // keeps a consistent view (mirrors ChangeThread in kthread.cpp).
    KThread* saved = KThread::currentThread();
    {
        KProcessPtr process = KProcess::create();
        // Boot env as the base, with the launcher's per-spawn env merged over it
        // (O2 — see mergeSpawnEnv): spawned apps keep the full boot wine env AND
        // honor per-spawn overrides such as WINEDEBUG.
        KThread* thread = process->startProcess(g_sessionCtx.workingDir, req.argv, mergeSpawnEnv(g_sessionCtx.env, req.env),
                                                g_sessionCtx.userId, g_sessionCtx.groupId,
                                                g_sessionCtx.effectiveUserId,
                                                g_sessionCtx.effectiveGroupId);
        if (!thread) {
            klog("wine64session: startProcess FAILED");
        } else {
            g_lastSpawnedPid.store(process->id, std::memory_order_release);
            klog_fmt("wine64session: spawned pid=%d", (int)process->id);
        }
    }
    KThread::setCurrentThread(saved);
}

// doSignalWineboot: signal __wineboot_event in wineserver32 via the Wine server
// protocol, bypassing 32-bit PE execution (which is broken - no 32-bit PE runs
// its entry point). This unblocks 32-bit Wine threads waiting on the event.
//
// Protocol (from Wine server/protocol.def):
//   REQ_open_event = 33: access(U32), attributes(U32), rootdir(U32), VARARG(name,unicode_str)
//   REQ_event_op = 31: handle(U32), op(S32)  [op: 0=PULSE, 1=SET, 2=RESET]
// Runs on the main-loop thread.
// doSignalWineboot: signal __wineboot_event in wineserver32 via the Wine server
// protocol, bypassing 32-bit PE execution (which is broken - no 32-bit PE runs
// its entry point). This unblocks 32-bit Wine threads waiting on the event.
//
// Protocol (verified against 64-bit traces):
//   REQ_open_event = 33: access(U32), attributes(U32), rootdir(U32), VARARG(name,unicode_str)
//   REQ_event_op = 31: handle(U32), op(S32)  [op=2 signals per 64-bit trace]
// Read with a bounded timeout so a dead/unresponsive wineserver cannot hang
// the session thread forever. Temporarily switches the socket to non-blocking
// and polls; restores the original blocking mode before returning.
// Returns bytes read, or 0 on timeout/closed. Logs on timeout.
// Runs on the main-loop thread.
static U32 readWithTimeout(KUnixSocketObject* sock, U8* buffer, U32 len, int timeoutMs) {
    bool wasBlocking = sock->isBlocking();
    sock->setBlocking(false);
    U32 result = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        result = sock->readNative(buffer, len);
        if (result != (U32)(0 - K_EWOULDBLOCK)) {
            break; // data, EOF(0), EPIPE, or EINTR -- all terminal
        }
        result = 0; // normalize EWOULDBLOCK for the timeout path
        if (std::chrono::steady_clock::now() >= deadline) {
            klog_fmt("wine64session: doSignalWineboot: read timed out after %d ms, giving up", timeoutMs);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    sock->setBlocking(wasBlocking);
    return result;
}

// Runs on the main-loop thread.
static void doSignalWineboot() {
    klog("wine64session: doSignalWineboot: signaling __wineboot_event in wineserver32");

    const int REQ_open_event = 33;
    const int REQ_event_op = 31;
    const int OP_SIGNAL = 2;

    KThread* thread = KThread::currentThread();
    if (!thread || !thread->process) {
        klog("wine64session: doSignalWineboot: no current thread/process");
        return;
    }

    // Step 1: Find wineserver32 socket path
    // Pattern: /tmp/wine-XXX/server32-UID-HASH/socket (32-bit isolated)
    // If not found yet (wineserver32 still starting), re-queue for next tick
    // We validate that the socket file actually exists with a live listening
    // socket kobject, trying all candidates (stale dirs from crashed servers
    // may linger alongside the live one).
    BString socketPath;
    {
        std::shared_ptr<FsNode> tmpNode = Fs::getNodeFromLocalPath(B("/"), B("/tmp"), true);
        if (tmpNode) {
            std::vector<std::shared_ptr<FsNode>> tmpChildren;
            tmpNode->getAllChildren(tmpChildren);
            for (auto& wineDir : tmpChildren) {
                if (!wineDir->name.startsWith(B("wine-"))) continue;
                BString winePath = B("/tmp/") + wineDir->name;
                std::shared_ptr<FsNode> wineNode = Fs::getNodeFromLocalPath(B("/"), winePath, true);
                if (!wineNode) continue;
                std::vector<std::shared_ptr<FsNode>> wineChildren;
                wineNode->getAllChildren(wineChildren);
                for (auto& child : wineChildren) {
                    if (!child->name.startsWith(B("server32-"))) continue;
                    BString cand = winePath + B("/") + child->name + B("/socket");
                    // Validate: socket node must exist with a live UNIX socket kobject
                    std::shared_ptr<FsNode> sockNode = Fs::getNodeFromLocalPath(B("/"), cand, true);
                    if (sockNode) {
                        std::shared_ptr<KObject> ko = sockNode->kobject.lock();
                        if (ko && ko->type == KTYPE_UNIX_SOCKET) {
                            socketPath = cand;
                            break;
                        } else {
                            klog_fmt("wine64session: doSignalWineboot: candidate %s has no live socket, skipping", cand.c_str());
                        }
                    }
                }
                if (!socketPath.isEmpty()) break;
            }
        }
    }
    if (socketPath.isEmpty()) {
        static int retryCount = 0;
        if (retryCount < 100) {
            retryCount++;
            Wine64Req req;
            req.kind = Wine64Req::SignalWineboot;
            pendingLockAcquire();
            g_pending.push_back(std::move(req));
            pendingLockRelease();
            klog_fmt("wine64session: doSignalWineboot: socket not ready, re-queued");
        } else {
            klog("wine64session: doSignalWineboot: giving up after 100 retries");
            retryCount = 0;
        }
        return;
    }
    klog_fmt("wine64session: doSignalWineboot: socket %s", socketPath.c_str());

    // Step 2: Create socket and allocate fd
    std::shared_ptr<KUnixSocketObject> sock = std::make_shared<KUnixSocketObject>(K_AF_UNIX, K_SOCK_STREAM, 0);
    KFileDescriptorPtr fd = thread->process->allocFileDescriptor(sock, 3, 0, -1, 0);
    if (!fd) {
        klog("wine64session: doSignalWineboot: fd alloc failed");
        return;
    }
    // Get the fd number - allocFileDescriptor returns the object, need the handle
    // We'll find it by scanning (or use a simpler approach below)

    // Step 3: Write sockaddr_un to guest memory and connect
    // sockaddr_un: family(U16=1) + path(108 bytes)
    U32 addrLen = 2 + socketPath.length() + 1;
    if (addrLen > 110) {
        klog("wine64session: doSignalWineboot: socket path too long");
        return;
    }
    // Allocate guest memory for sockaddr
    U32 guestAddr = thread->memory->mmap(thread, 0, 4096, 7, 0x22, -1, 0, false);
    if (!guestAddr) {
        klog("wine64session: doSignalWineboot: mmap failed");
        return;
    }
    thread->memory->writew(guestAddr, K_AF_UNIX);
    thread->memory->memcpy(guestAddr + 2, socketPath.c_str(), socketPath.length() + 1);

    // Find the fd number for our allocated descriptor
    // allocFileDescriptor with handle=-1 auto-assigns; we need to find it
    // Simpler: iterate fds to find our socket object
    U32 fdNum = 0;
    for (U32 i = 3; i < 1024; i++) {
        KFileDescriptorPtr f = thread->process->getFileDescriptor(i);
        if (f && f->kobject == sock) {
            fdNum = i;
            break;
        }
    }
    if (!fdNum) {
        klog("wine64session: doSignalWineboot: could not find fd number");
        return;
    }

    // Connect with timeout: wineserver32 may not be in its accept loop yet.
    // A blocking connect() waits indefinitely on the acceptor; use non-blocking
    // + poll to avoid hanging the session thread forever.
    sock->setBlocking(false);
    U32 connResult = sock->connect(thread, fd, guestAddr, addrLen);
    bool connected = (connResult == 0);
    if (!connected && connResult == (U32)(0 - K_EINPROGRESS)) {
        // Connection queued; wait for acceptor to establish it (sets sock->connection)
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
        while (std::chrono::steady_clock::now() < deadline) {
            if (!sock->connection.expired()) {
                connected = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!connected) {
            klog("wine64session: doSignalWineboot: connect timed out after 5000 ms, re-queued");
        }
    }
    sock->setBlocking(true); // restore blocking for protocol I/O
    if (!connected) {
        if (connResult != 0 && connResult != (U32)(0 - K_EINPROGRESS)) {
            klog_fmt("wine64session: doSignalWineboot: connect failed errno=%d (%s)", (int)(S32)connResult, socketPath.c_str());
        }
        // Socket may have died between validation and connect, or wineserver32
        // not yet accepting; re-queue to retry
        {
            Wine64Req req;
            req.kind = Wine64Req::SignalWineboot;
            pendingLockAcquire();
            g_pending.push_back(std::move(req));
            pendingLockRelease();
            klog("wine64session: doSignalWineboot: connect failed, re-queued");
        }
        return;
    }
    klog("wine64session: doSignalWineboot: connected");

    // Step 3b: Send init_first_thread to establish protocol session
    const int REQ_init_first_thread = 1;
    U8 initReq[32];
    *(int*)(initReq + 0) = REQ_init_first_thread;
    *(int*)(initReq + 4) = 20;
    *(int*)(initReq + 8) = 32;
    *(unsigned int*)(initReq + 12) = 9999;
    *(unsigned int*)(initReq + 16) = 9999;
    *(int*)(initReq + 20) = 0;
    *(int*)(initReq + 24) = -1;
    *(int*)(initReq + 28) = -1;
    U32 initWritten = sock->writeNative(initReq, 32);
    klog_fmt("wine64session: doSignalWineboot: init written %u/32", initWritten);
    if (initWritten == 32) {
        U8 initReply[64];
        U32 initRead = readWithTimeout(sock.get(), initReply, 32, 5000);
        klog_fmt("wine64session: doSignalWineboot: init reply %u bytes", initRead);
    }

    // Step 4: Send open_event
    const char* eventName = "\\\\KernelObjects\\\\__wineboot_event";
    int nameLen = 0;
    while (eventName[nameLen]) nameLen++;
    int nameBytes = (nameLen + 1) * 2;
    int reqSize = 28 + nameBytes;
    U8* reqBuf = new U8[reqSize];
    *(int*)(reqBuf + 0) = REQ_open_event;
    *(int*)(reqBuf + 4) = nameBytes;
    *(int*)(reqBuf + 8) = 8;
    *(unsigned int*)(reqBuf + 12) = 0x1F0003;
    *(unsigned int*)(reqBuf + 16) = 0;
    *(unsigned int*)(reqBuf + 20) = 0;
    *(unsigned int*)(reqBuf + 24) = 0;
    U16* namePtr = (U16*)(reqBuf + 28);
    for (int i = 0; i <= nameLen; i++) namePtr[i] = (U16)eventName[i];

    U32 written = sock->writeNative(reqBuf, reqSize);
    delete[] reqBuf;
    if (written != (U32)reqSize) {
        klog("wine64session: doSignalWineboot: open_event write failed");
        return;
    }

    // Step 5: Read reply
    U8 replyBuf[32];
    U32 nread = readWithTimeout(sock.get(), replyBuf, 8, 5000);
    if (nread < 8) {
        klog("wine64session: doSignalWineboot: open_event reply short");
        return;
    }
    U32 eventHandle = *(U32*)(replyBuf + 0);
    if (!eventHandle) {
        klog("wine64session: doSignalWineboot: null handle");
        return;
    }
    klog("wine64session: doSignalWineboot: got event handle");

    // Step 6: Send event_op
    U8 opReq[20];
    *(int*)(opReq + 0) = REQ_event_op;
    *(int*)(opReq + 4) = 8;
    *(int*)(opReq + 8) = 8;
    *(U32*)(opReq + 12) = eventHandle;
    *(int*)(opReq + 16) = OP_SIGNAL;
    written = sock->writeNative(opReq, 20);
    if (written != 20) {
        klog("wine64session: doSignalWineboot: event_op write failed");
        return;
    }
    nread = readWithTimeout(sock.get(), replyBuf, 8, 5000);
    if (nread < 8) {
        klog("wine64session: doSignalWineboot: event_op reply short");
        return;
    }
    klog("wine64session: doSignalWineboot: event signaled successfully");
}


// Kill a previously-spawned app (close-previous-on-switch). Runs on the
// main-loop thread.
//
// HOW (and why not a signal): delivering a guest SIGTERM means running wine's
// guest signal handler mid-session — the path that crashed the runtime
// (WebAssembly.Exception + wineserver protocol errors) when kill-on-switch was
// first attempted. Instead this terminates the process KERNEL-SIDE, the same
// way exit_group does: mark every thread `terminating` and wake any wait it's
// parked in. Each thread's own platform pthread then unwinds and the LAST one
// runs the full process cleanup in KProcess::deleteThread (fds close — so the
// persistent wineserver sees its client die, like any crashed Windows app —
// RAM released, process erased). CPU-bound threads that never block notice the
// flag via the periodic check in CPU64::run(). Deliberately NON-blocking: we
// don't wait for the threads to finish dying (terminateOtherThread's wait loop
// would wedge the main loop if a thread were stuck), we just flag + wake and
// let them exit on their own pthreads.
//
// The host-side X presence (its connections/windows on our wire server) is
// dropped immediately — the dying process never tells the X server anything.
static bool terminatePid(U32 pid, const char* why) {
    if (!pid) return false;
    KProcessPtr process = KSystem::getProcess(pid);
    if (!process || process->terminated) {
        klog_fmt("wine64session: kill pid=%d (%s) — already gone", (int)pid, why);
        bw64DropAppPresenceByPid(pid);  // in case its X state still lingers
        return false;
    }
    // Never kill the session's plumbing, whatever pid bookkeeping claims: the
    // wineserver (pinned persistent — killing it ends the session) or wine's
    // service processes. The X-derived pid below SHOULD always be a real app,
    // but a guard here is cheap and the failure mode without it is fatal.
    BString cmd = process->commandLine;
    if (cmd.contains("wineserver") || cmd.contains("services.exe") ||
        cmd.contains("winedevice") || cmd.contains("plugplay") ||
        cmd.contains("rpcss") || cmd.contains("explorer.exe") ||
        cmd.contains("deskpin.exe")) {
        klog_fmt("wine64session: REFUSING to kill pid=%d (%s) — system process (%s)",
                 (int)pid, cmd.c_str(), why);
        return false;
    }
    klog_fmt("wine64session: killing previous app pid=%d (%s) [%s]", (int)pid,
             cmd.c_str(), why);
    process->iterateThreads([](KThread* thread) {
        thread->terminating = true;
        // Wake the thread if it's parked in a guest wait so it sees the flag
        // (same wake terminateOtherThread uses, minus the blocking wait).
        const std::shared_ptr<BoxedWineCondition> cond = thread->waitingCond;
        if (cond) {
            cond->lock();
            cond->signal();
            cond->unlock();
        }
        return true;
    });
    bw64DropAppPresenceByPid(pid);
    return true;
}

static void doKill(U32 pid) {
    // The pid JS tracked (the spawn root). For an in-session spawn this IS the
    // app; for the boot app it's usually a relay ancestor that already exited.
    terminatePid(pid, "launcher-tracked pid");
    // The pid that owns the window being switched away from, per the X server —
    // authoritative even when wine relayed the app through intermediate
    // processes (observed: the BOOT notepad's GUI lived in a different pid than
    // the boot spawn, so killing only the tracked pid left it running hidden).
    U32 xpid = bw64TakeOutgoingAppPid();
    if (xpid && xpid != pid) {
        terminatePid(xpid, "X window owner");
    }
}

// Register a file that JS just wrote directly into the Emscripten MEMFS (e.g. an
// uploaded .exe) into Boxedwine's VFS, so newly-spawned wine processes can find
// it. WHY THIS IS NEEDED: FsNode::loadChildren() scans a directory's native
// contents exactly ONCE (gated by hasLoadedChildrenFromFileSystem) and caches the
// child nodes; the prefix dir /home/username is scanned at boot. A later
// Module.FS.writeFile() lands in MEMFS but is invisible to that cache, so wine's
// path resolver returns c0000135 ("cannot find binary"). Here we look up the
// parent dir node and, if the child isn't already registered, add a node for it
// (its native path = parent's native path + the remote-encoded name), matching
// what loadChildren() would have done. Runs on the main-loop thread.
static void doRegisterFile(const BString& guestPath) {
    if (guestPath.isEmpty()) return;
    // Split into parent dir + leaf name.
    int slash = guestPath.lastIndexOf('/');
    if (slash < 0) { klog_fmt("wine64session: registerFile bad path '%s'", guestPath.c_str()); return; }
    BString parentPath = (slash == 0) ? B("/") : guestPath.substr(0, slash);
    BString name = guestPath.substr(slash + 1);
    if (name.isEmpty()) return;

    std::shared_ptr<FsNode> parent = Fs::getNodeFromLocalPath(B(""), parentPath, false);
    if (!parent) {
        klog_fmt("wine64session: registerFile parent '%s' not in VFS", parentPath.c_str());
        return;
    }
    // Force the parent's children to be loaded first (so getChildByName is valid
    // and we don't double-register what loadChildren already saw).
    if (parent->getChildByName(name)) {
        klog_fmt("wine64session: registerFile '%s' already present — ok", guestPath.c_str());
        return;
    }
    // Native path = parent's native path + the remote-encoded leaf name (mirrors
    // FsNode::loadChildren's localName->remoteName mapping for on-disk names).
    BString remoteName = name;
    Fs::localNameToRemote(remoteName);
    BString nativePath = parent->nativePath.stringByApppendingPath(remoteName);
    Fs::addFileNode(guestPath, B(""), nativePath, false, parent);
    klog_fmt("wine64session: registerFile '%s' -> native '%s' (registered)",
             guestPath.c_str(), nativePath.c_str());
}

// Drain queued requests. MUST be called from the main-loop thread (mainloop.cpp
// calls it once per tick). Swaps the queue out under the spinlock, then runs the
// requests outside the lock so kernel ops can take their own locks freely.
void bw64SessionDrainSpawns() {
    if (!g_sessionCtx.valid.load(std::memory_order_acquire)) return;
    std::vector<Wine64Req> todo;
    pendingLockAcquire();
    if (!g_pending.empty()) todo.swap(g_pending);
    pendingLockRelease();
    for (auto& req : todo) {
        if (req.kind == Wine64Req::Kill) doKill(req.pid);
        else if (req.kind == Wine64Req::RegisterFile) doRegisterFile(req.path);
        else if (req.kind == Wine64Req::SignalWineboot) doSignalWineboot();
        else doSpawn(req);
    }
}

// --- exported JS entry points -------------------------------------------------
// bw64_spawn: queue `wine64 <app>` to be launched into the running kernel.
// Called via Module.ccall("bw64_spawn","number",["string","string"],[argv,env]).
// Returns 1 if queued (session up), 0 if the session isn't ready yet. NON-
// blocking — safe to call from the browser main thread.
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_spawn(const char* argvJoined, const char* envJoined) {
    if (!g_sessionCtx.valid.load(std::memory_order_acquire)) return 0;
    Wine64Req req;
    req.kind = Wine64Req::Spawn;
    req.argv = splitLines(argvJoined);
    req.env = splitLines(envJoined);
    if (req.argv.empty()) return 0;
    pendingLockAcquire();
    g_pending.push_back(std::move(req));
    pendingLockRelease();
    return 1;
}

// bw64_xwire_key: inject one key event into the guest's XWire input queue.
// Called via Module.ccall("bw64_xwire_key",null,["number","number"],[scancode,down]).
// scancode is an SDL scancode (USB HID usage); down!=0 presses, 0 releases.
// Lets the browser page drive guest keyboard input headless (-novideo), where
// Emscripten SDL never captures DOM keys. Safe from the browser main thread:
// the sink queue push is mutex-guarded. No-op until the session is up.
extern "C" EMSCRIPTEN_KEEPALIVE void bw64_xwire_key(int sdlScancode, int down) {
    xwireInjectKey((uint32_t)sdlScancode, down);
}

// bw64_xwire_mouse_move(x, y): absolute pointer position in guest window coords.
// bw64_xwire_mouse_rel(dx, dy): relative deltas (FPS mouse-look).
// bw64_xwire_mouse_button(button, down): X11 button 1=left,2=middle,3=right,8=X1,9=X2.
// bw64_xwire_mouse_wheel(dx, dy): wheel ticks (dy>0 up, dy<0 down, dx horizontal).
// All no-ops until the session is up. Safe from the browser main thread.
extern "C" EMSCRIPTEN_KEEPALIVE void bw64_xwire_mouse_move(int x, int y) {
    xwireInjectMouseMove(x, y);
}
extern "C" EMSCRIPTEN_KEEPALIVE void bw64_xwire_mouse_rel(int dx, int dy) {
    xwireInjectMouseRel(dx, dy);
}
extern "C" EMSCRIPTEN_KEEPALIVE void bw64_xwire_mouse_button(int button, int down) {
    xwireInjectMouseButton(button, down);
}
extern "C" EMSCRIPTEN_KEEPALIVE void bw64_xwire_mouse_wheel(int dx, int dy) {
    xwireInjectMouseWheel(dx, dy);
}

// bw64_register_file: register a file JS already wrote into MEMFS (e.g. an
// uploaded .exe at guest path /home/username/foo.exe) into Boxedwine's VFS, so the
// app spawned right after can find it. Queue it (drained on the main-loop thread,
// in order) so it runs BEFORE the spawn JS queues next. NON-blocking; returns 1 if
// queued. Called via Module.ccall("bw64_register_file","number",["string"],[path]).
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_register_file(const char* guestPath) {
    if (!g_sessionCtx.valid.load(std::memory_order_acquire) || !guestPath || !guestPath[0]) return 0;
    Wine64Req req;
    req.kind = Wine64Req::RegisterFile;
    req.path = BString::copy(guestPath);
    pendingLockAcquire();
    g_pending.push_back(std::move(req));
    pendingLockRelease();
    return 1;
}

// bw64_kill: queue a kernel-side terminate of a previously-spawned app pid
// (app switch kills the outgoing app). The launcher queues the NEW app's spawn
// first, then this — the FIFO drain keeps them ordered, so a failed spawn can
// still be retried against a live old app. NON-blocking. Returns 1 if queued.
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_kill(int pid) {
    if (!g_sessionCtx.valid.load(std::memory_order_acquire) || pid <= 0) return 0;
    Wine64Req req;
    req.kind = Wine64Req::Kill;
    req.pid = (U32)pid;
    pendingLockAcquire();
    g_pending.push_back(std::move(req));
    pendingLockRelease();
    return 1;
}

// bw64_last_spawned_pid: the pid of the most recently spawned app, so JS can
// remember which process to kill on the next switch.
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_last_spawned_pid() {
    return (int)g_lastSpawnedPid.load(std::memory_order_acquire);
}

// bw64_pid_alive: whether a pid is still running (1) or has exited (0).
// Used by JS to wait for helper processes (e.g. wineboot --init on x86)
// without polling logs. Note: exited processes linger in the process table
// as zombies until reaped, so table membership alone is not enough -- check
// isTerminated().
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_pid_alive(int pid) {
    if (pid <= 0) return 0;
    KProcessPtr proc = KSystem::getProcess((U32)pid);
    return (proc && !proc->isTerminated()) ? 1 : 0;
}

// bw64_reset_present: clear the XWire base window so the next app's first window
// becomes the presented base (instead of compositing onto the closed app's
// larger window). Safe to call from the browser main thread — it locks the
// XWire registry mutex internally and touches no per-thread kernel state.
extern "C" EMSCRIPTEN_KEEPALIVE void bw64_reset_present() {
    bw64ResetPresentForSwitch();
}

// Whether the persistent session is up (context captured == boot completed).
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_session_ready() {
    return g_sessionCtx.valid.load(std::memory_order_acquire) ? 1 : 0;
}

// bw64_signal_wineboot_event: queue a request to signal __wineboot_event in
// wineserver32 via the Wine server protocol. Bypasses 32-bit PE execution
// (which is broken). Called via Module.ccall("bw64_signal_wineboot_event").
// Returns 1 if queued, 0 if session not ready.
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_signal_wineboot_event() {
    if (!g_sessionCtx.valid.load(std::memory_order_acquire)) return 0;
    Wine64Req req;
    req.kind = Wine64Req::SignalWineboot;
    pendingLockAcquire();
    g_pending.push_back(std::move(req));
    pendingLockRelease();
    return 1;
}

// bw64_clipboard_set: stage host clipboard text for the guest (paste path).
// Safe from the browser main thread — only takes the XWire registry mutex and
// queues the SelectionClear for the main-loop pump. Called via
// Module.ccall("bw64_clipboard_set", null, ["string"], [text]).
extern "C" EMSCRIPTEN_KEEPALIVE void bw64_clipboard_set(const char* text) {
    bw64ClipboardSetHost(text);
}

// bw64_clipboard_get: the latest guest copy (harvested on SetSelectionOwner)
// or whatever the host last staged. Called via
// Module.ccall("bw64_clipboard_get", "string", [], []) — ccall copies the
// returned C string immediately, so the static buffer lifetime is fine.
extern "C" EMSCRIPTEN_KEEPALIVE const char* bw64_clipboard_get() {
    return bw64ClipboardGetHost();
}

#endif
