# Cleanup wave — kernel event/wait/socket/process-lifecycle lane record

- **Branch:** `lane-cleanlog-ksubsys` (worktree `wt-cleanlog-ksubsys`), base = integration tip `538f03db5`.
- **Standard followed:** `docs/failure-logging-and-recovery.md` (pathfinder lane, branch `lane-cleanlog`) and the clean-up skill. One `BW64-FAIL key=value` line at the point of failure; reporting never gated behind the recovery flag; recovery ladder degrade → report-and-continue → fail loud and fast, at the smallest possible scope.
- **Verification:** native suite run **serially** (`Build/Test/boxedwine 0 0 1`): **649 passed / 0 failed** (baseline at base = 648; +1 is this lane's new test). Multi-threaded runs of the suite die at exit 139 on the loaded box — pre-existing and box-wide (other lanes hit the same); serial is the trustworthy mode. JS suite not applicable (no JS touched).

## Why this area mattered

In the default configuration `kpanic` prints and calls `exit(1)`: **the whole emulator dies**, not just the guest program. Every guest-reachable `kpanic` below was therefore a full-emulator kill switch handed to untrusted guest input — the same failure class as the dead-start bug that prompted this campaign, except silent in a different way: the guest's ordinary API misuse (a short sockopt length, a poll set containing an epoll fd) took down every program in the session.

## Commits in this lane

1. `bac393b8f` kepoll: degrade instead of kpanic on guest-reachable object operations
2. `4cf569006` kevent/ksignal: graceful answers for eventfd and signalfd objects
3. `d71a2573f` kinotify: log watch failures, correct the seek answer
4. `6c5842005` knetlink: guest option lengths and flags degrade instead of kpanic
5. `4293d192e` knativesocket: guest option lengths degrade; monitor skips the unmonitorable
6. `4b3a41e54` kunixsocket: message-path panics degrade; sendto/recvfrom stop lying
7. `282060c4e` kprocess: lifecycle panics degrade; ioctl reporting ungated
8. `ed4dcc00b` test: regression coverage for graceful kernel-object answers
9. This record.

## What changed, per subsystem

### kepoll (`kepoll.cpp`, `kepoll.h`)
- The seven `KObject` readiness/io stubs that `kpanic`'d are now real or graceful. `kpoll.cpp` calls `waitForEvents`/`isReadReady`/`isWriteReady` on **every** registered object, so any poll set containing an epoll fd (or an epoll fd inside `epoll_wait`) killed the emulator.
  - `waitForEvents` forwards wakeup registrations to the registered objects and records parent conditions (new registry under the waiters mutex); `ctl` ADD forwards to later registrations, DEL unregisters, and wakeups also signal recorded parents — poll-on-epoll now actually wakes.
  - `isReadReady` scans registrations' readiness (IN→readable, PRI→priority, OUT→writable, closed target→ready) with a nesting cap of 5 (Linux's `ELOOP` bound) so registration cycles degrade instead of recursing forever.
  - `isWriteReady` returns false; `readNative`/`writeNative` return `-EINVAL`; `stat` returns `-ENOTSUP`; all log.
  - `setBlocking` stores the flag (new `blocking` member) instead of panicking — this also fixes `F_GETFL` reporting and the panic when a guest clears `O_NONBLOCK` on an epoll fd.
- `ctl` failures (EBADF / bad-events / bad-exclusive / bad-op / EINVAL / EEXIST / ENOENT) log one shared `BW64-FAIL` line with op/fd/events values.
- `ioctl` returns ENOTTY (reporting lives at the single dispatch point, see kprocess).
- Seek's stale ":TODO: is this right?" replaced with the verified ESPIPE rationale.

### kevent / ksignal (`kevent.cpp`, `ksignal.cpp`)
- `KEvent::stat`, `KSignal::stat` → `-ENOTSUP` + log (were `kpanic`); `KSignal::isWriteReady` → false (was `kpanic`); signalfd `waitForEvents` POLLOUT arm never registers + logs once (was `kpanic`); `KSignal::writeNative` → `-EINVAL` + log.
- `syscall_signalfd4`: flag and mask-size validations were **gated behind the recovery flag** — by default bad flags were silently accepted and an unknown mask size fell through to a `kpanic`. Both now answer `-EINVAL` in every configuration; the tail `kpanic` became an unreachable defensive `-EINVAL`.
- `eventfd2` bad flags now log; seek ESPIPE TODOs resolved in both files.

### kinotify (`kinotify.cpp`)
- `addWatch` ENOTDIR/EEXIST and `removeWatch` EINVAL now log with mask/wd values; ioctl/write/writeNative/stat/setLock stubs log once per case.
- **Semantics correction:** `seek` returned 0 (silent wrong success); inotify fds are anon-inode and Linux answers `-ESPIPE`. Now `-ESPIPE`.

### knetlink (`knetlink.cpp`)
- Eight sockopt length `kpanic`s → one shared helper: log once per option + `-EINVAL`.
- `setsockopt` unsupported option/level: the EINVAL was recovery-gated and default mode **fell through to success**; now answered in every configuration.
- `sendmsg` `kpanic` → `-EOPNOTSUPP` + log. `recvfrom` any-nonzero-flags `kpanic` → `MSG_DONTWAIT` honored (EAGAIN on empty buffer), `MSG_NOSIGNAL` ignored, anything else `-EINVAL` + log.

### knativesocket (`knativesocket.cpp`)
- All 30 setsockopt/getsockopt option-length `kpanic` statements → one file-static helper (log once per option, `-EINVAL`). One panic message named the wrong option (MULTICAST_TTL at the `IP_MTU_DISCOVER` case); the helper call carries the correct name.
- `updateWaitingList` FD_SETSIZE `kpanic_fmt` (which also had a `%s`-with-int format bug) → skip that socket + log; its readiness degrades to poll timeouts.
- Both emscripten monitor-pipe `kpanic`s → log + do not start the monitor thread.

### kunixsocket (`kunixsocket.cpp`)
- `UnixSocketNode::removeDir` → `-ENOTDIR` + log; `connect` over-long guest addrlen → `-ENAMETOOLONG` + log; `connect` non-UNIX domain → `-EAFNOSUPPORT` + log; `sendmsg` on unconnected datagram → `-EDESTADDRREQ` + log; three sendmsg cmsg validation `kpanic_fmt`s → `-EINVAL` + log each.
- `recvmsg`: a stored segment larger than the receive iov is truncated to fit and logged (was `kpanic`); length-prefixed storage keeps the message walk in sync.
- **Silent lies fixed:** `sendto` returned 0 without sending anything — which also made `send()` on a unix socket a silent no-op, since `ksend` routes through sendto. A connected send now goes through the write path; a destination address answers `-EDESTADDRREQ` + log. `recvfrom` with a source-address buffer returned 0 without reading; it now reads and reports an unnamed AF_UNIX peer address (family + addrlen 2), as Linux does for unbound peers.

### kprocess (`kprocess.cpp`)
- `execve`: when the new program fails to load after the old image was already reset, the exec can neither succeed nor report back to a live caller. Was `kpanic` (whole emulator). Now logs `BW64-FAIL exec-load` and the **process** exits via `exitgroup` (which also releases a vfork parent, as a child that exits without exec'ing would).
- 32-bit exec stack setup: argv/envp pointer arrays were fixed stack arrays with a `kpanic` past 1024 entries; sized `std::vector`s remove the arbitrary cap (and its now-dead `#define`); the guest stack remains the real limit, as on Linux.
- `dup2` onto a descriptor with extra references (another thread blocked in a call on it) `kpanic`'d; the table entry is now replaced with a `BW64-FAIL` line and the shared_ptr keeps the old descriptor alive for its remaining holders — the same lifetime rule as every other table removal.
- `KProcess::ioctl` is the single dispatch point for guest ioctls; its ENOTTY reporting was gated behind the recovery flag and is now loud in both configurations (per-object ioctl log lines added earlier in this lane were removed again so reporting has exactly one home).

### Regression test (`source/test/process/testKernelGrace.cpp`)
Covers: epoll setBlocking roundtrip, isWriteReady, readNative/writeNative/stat errnos, ctl EEXIST/EINVAL/EBADF/ENOENT, nested epoll readiness, `poll()` on an epoll fd before/after an eventfd write, signalfd bad mask size and bad flags, KSignal object stubs + POLLOUT wait arm, netlink setsockopt/getsockopt len 3, native setsockopt IP_TTL len 3, unix socketpair sendto delivery, recvfrom-with-address payload + unnamed peer, sendmsg bad cmsg level.
**Sabotage proof:** restoring the signalfd4 mask-size `kpanic` makes the test binary die inside this test (exit 1, the panic message) at exactly that call; restored, the window and the full suite are green.

## Audited, deliberately left alone

- **`KProcess::getNextFileDescriptorHandle` fd-exhaustion `kpanic`** (0x4000-fd ceiling): answering EMFILE safely requires changing the alloc contract across ~50 `allocFileDescriptor` call sites whose callers mostly dereference the result immediately. Flagged as a follow-up for a dedicated lane; converting it naively would trade a loud failure for null derefs.
- **`allocSHM` index-space `kpanic`**: guards against a wrap-around infinite loop past 2^31 live segment indexes; practically unreachable. Kept as a genuine internal-invariant panic.
- **kepoll/kevent/ksignal/kinotify `map()` and `length()` stubs**: unreachable — `canMap()` gates mmap upstream (`kmemory.cpp`), and these objects never claim mappability. Audited tree-wide; left as-is.
- **kinotify queue-overflow event coalescing**: matches Linux behavior (overflow is reported via the queue-overflow event); not a silent failure.
- **knetlink getsockopt default branches**: already loud (log + ENOPROTOOPT) before this lane.
- **kunixsocket `bind` empty-name return-0**: existing working behavior that X.org depends on (noted in its TODO); changing the answer risks a working consumer for a cosmetic gain. Left as behavior, unchanged.
- **Instruction-skip recovery (`BW64_RECOVER`)** stays opt-in, per the pathfinder standard — actions may be gated; reporting may not. Everywhere this lane ungated something, it ungated only the reporting/answer, never a recovery action.

## Residuals (honest limits)

- Unix `sendto`: `MSG_NOSIGNAL` is accepted through the write path but not honored (no SIGPIPE suppression plumbing there yet), and `MSG_DONTWAIT` is governed by the socket's blocking flag. Both are noted in code comments at the site.
- Once-per-case log lines carry the **first** occurrence's values; later repeats of the same case are the same line by design (pathfinder rate-limit shape).
- The fd-exhaustion EMFILE conversion (above) is the one known remaining guest-reachable emulator-kill in this lane's area.

## Process note

Mid-lane, this lane's first foreground build lost its SSH channel; the remote make survived and briefly raced a second make in the **same** tree. Both were killed and the tree rebuilt clean from scratch (the build whose results are reported above). No other lane's processes were touched — the neighboring lane's emscripten build was verified unharmed, and one `pkill` self-match against this lane's own shell was caught and replaced with bracket-guarded patterns.

## Lane hygiene

Generated build state (`project/linux/Build`) stripped from this tree before finishing; nothing pushed; live site untouched.
