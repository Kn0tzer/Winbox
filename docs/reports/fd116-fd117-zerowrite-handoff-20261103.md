# FD-116..117 — the zero-length plain write and the handoff slice test (2026-11-03)

Branch `lane-fd116-117`, based on `lane-fd113-115` tip
`4322652a7`. Commits: `7c6ce04ae` (the FD-116 guest test,
RED on the base, and the FD-117 harness completion),
`b9050125e` (the test's 64-bit flag for the closed-peer
stream case), `76eeb0adc` (the FD-116 fix), plus the
register/record commit carrying this file.

Numbering note: this branch's entry numbers are its own
lineage's. The batch-4 merge audit renumbers in filing
order — the wedge filing first, then the fd105-108 lane's
four filings, then the fd113-115 lane's FD-113, FD-114,
FD-115, FD-116 and FD-117, then this lane's FD-118 and
FD-119. Closures below are CL-67 (FD-116) and CL-68
(FD-117) provisionally, this lineage's next after CL-66.
Each defect is therefore also described by its substance.

## FD-116 — an empty write on a message socket queues the empty message

Substance: on the host, write(fd, buf, 0) on a connected
datagram socket is a real send of length zero: it returns
0 and the peer's recv returns 0 for an empty datagram.
The 64-bit dispatcher's sys_write64 returned 0 on a zero
count before the descriptor was even resolved, so the
kobject never saw the call and nothing queued — where
every other emulator write path (the 32-bit write, and
sendmsg per FD-112) queues the empty datagram.

Host measurement (`test-results/fd116-117/hostref-fd116.c`,
box kernel 6.17.0-1020-oracle), the order the host checks
in: an unknown or closed descriptor answers -EBADF; a
wrong-mode descriptor answers -EBADF on the host (mode
before buffer — a read-only file with a wild buffer still
answers -EBADF); the buffer address is then validated as
an address only — NULL, an unmapped user address and a
PROT_NONE page all queue the empty datagram, while an
address outside the user range (-1) answers -EFAULT with
nothing queued; the object's own answers follow — an
unconnected datagram socket -ENOTCONN, a stream socket 0
with nothing queued, a stream socket whose peer is gone
-EPIPE (zero and nonzero counts alike), a stream socket
after shutdown(SHUT_WR) -EPIPE, a datagram socket whose
peer is gone -ECONNREFUSED for the first send and
-ENOTCONN after, and a connected seqpacket socket queues
an empty record.

Fix (syscall64.cpp, sys_write64): the zero-count early
return is gone. A zero count stages nothing (the guest
copy is guarded by the count), runs the standing gates —
unknown descriptor -EBADF, wrong mode this write
family's standing -EINVAL — then answers -EFAULT for a
buffer address above the x86-64 user range
(0x00007FFFFFFFFFFF) with nothing queued, and reaches
the kobject's writeNative with length 0, which queues
the empty datagram on a datagram socket and answers 0
with nothing queued on every other object. Nonzero
counts keep the copy path exactly as it was. One
interaction worth recording: sys_writev64's per-segment
loop calls sys_write64 per segment, so a zero-length
segment now also reaches the object; on a stream socket
that still answers 0, and on a dead stream peer it now
takes the same -EPIPE path a nonzero write takes, which
is the host's shape. sys_pwrite64 was audited alongside:
it already resolves the descriptor and runs its mode
gate before its zero-count return, so the filed check
order already holds there (register appendix).

Proof: new x64 test testWriteZeroLength64Syscalls. RED
on the base (the run at `7c6ce04ae`'s parent): write0
answered 0 with the peer's recv at -EWOULDBLOCK on every
queueing case, 0 for the out-of-range buffer, 0 for an
unknown descriptor — the filed shape exactly. GREEN on
the fix. Two harness notes the test carries: a wrong-mode
descriptor is allocated over the pair's socket object
with K_O_RDONLY (the harness idiom), pinning this
lineage's -EINVAL mode answer; and the closed-peer
stream case flags the shared test process 64-bit for its
scope (the testDevInputJoystick pattern) because the
socket layer keys its broken-pipe split — a clean -EPIPE
for a 64-bit thread vs the 32-bit SIGPIPE path that
returns the -K_CONTINUE sentinel — on the current
thread's process width; with the flag set, the zero and
nonzero writes both answer -EPIPE, the host's answers.
The closed-peer datagram case pins the emulator's
standing -ENOTCONN; the host's one-shot -ECONNREFUSED is
filed as FD-119.

## FD-117 — the unix-connect-handoff slice test compiles again

Substance: the JS slice test
`web/tests/suite/unit/unix-connect-handoff.test.mjs`
extracts the KUnixSocketObject destructor, connect and
accept from kunixsocket.cpp and compiles them against a
stub class. FD-111's growth — the abstract-name registry
and one-way datagram links — outgrew the stub, so the
generated check.cpp no longer compiled and the test
failed. Product code was never implicated.

Reproduction first: on this lane's unmodified base the
test fails with the generated check.cpp not compiling
(`test-results/fd116-117/js-handoff-base.log`). Building
the extracted file standalone enumerates ten errors: the
destructor's abstractRegistered / abstractName members
and the abstractRegistryMutex / abstractRegistry
helpers, oneWayPeerLink in the destructor and twice in
connect, resolveNamedPeer called from connect's
datagram branch, and the S32 alias in connect's cast.
(recvBuffer and KThread::id, named in the filing, appear
only inside klog_fmt calls, which the harness defines as
a discarding macro, and produce no errors.)

Fix (the test file only): the stub now mirrors the
product's shape — `using S32 = int32_t` and
K_EPROTOTYPE (71) added to the prelude; the two registry
helpers declared before the stub struct and defined
after it (function-local statics, as in the product's
anonymous namespace); abstractName, abstractRegistered
and oneWayPeerLink members on the stub; and
resolveNamedPeer declared on the stub and defined with
the product's contract (short or non-UNIX address
-EINVAL; an abstract name resolved through the registry
with a type mismatch at -EPROTOTYPE and a missing or
expired entry at -ECONNREFUSED; a pathname resolved
through the filesystem node — no node -ENOENT, a node
whose socket is gone or of the wrong kind -ECONNREFUSED,
a type mismatch -EPROTOTYPE), reading guest bytes
through the stub memory's own memcpy. The test's
scenario drives a stream connect, which resolves through
the filesystem directly, so the resolver is compile-only
coverage here; the destructor's registry cleanup is
inert unless a stub socket registers, which none does.

Proof: the test passes standalone
(`test-results/fd116-117/js-handoff-green.log`) and in
the full JS unit suite below.

## Filed from this lane

- FD-118 — SOCK_SEQPACKET unix sockets are not
  modelled. The host queues an empty record for a
  seqpacket zero-length write and keeps record
  boundaries (measured in this lane's reference
  program). The emulator defines the type constant but
  implements nothing for it: socketpair answers -EINVAL
  for the type, and a singly created seqpacket socket
  takes the stream branch everywhere, so records merge
  into a byte stream.
- FD-119 — a datagram send to a peer whose descriptor
  was just closed answers -ENOTCONN, where the host
  answers -ECONNREFUSED for the first send after the
  close and -ENOTCONN after (measured in this lane's
  reference program). The peer object is destroyed with
  its descriptor, so no closed-but-remembered state
  survives for any send path or count.

## Verification

- Native suites serial, one worker: main 668/668 and
  x64 85/85, 0 FAILED (the x64 count is the base's 84
  plus testWriteZeroLength64Syscalls). Logs:
  `test-results/fd116-117/red-run.log` (the new test the
  one failure), `green-run.log`, `main-run.log`.
- JS unit on the built tree: 348 passed / 0 failed —
  the base's 347 passed / 1 failed, the one failure
  FD-117. Log: `test-results/fd116-117/js-unit.log`.
  The silent-failure allowance for syscall64.cpp stays
  120: the removed early return was never a counted
  site, and the added range-check answer is in the
  write family's reviewed shape, so the scanner's count
  does not move (no-silent-failure, no-silent-drops and
  silent-failure-scanner all pass unchanged).
- Browser build: BUILD COMPLETE, "Browser export
  verified"; BUILD_INFO git_sha `76eeb0adc` (the fix
  tip the build ran at), wasm sha256 `2fb5b2e3`, the
  staged dependency archive `c4dbe35986a5` as pinned.
  Log: `test-results/fd116-117/browser-build.log`.
- Build inputs, as the sibling records describe:
  bootstrap --local from the retained
  runtime-assets-20261101 archive (the tar file inside
  `/home/ubuntu/backups/winbox-runtime-assets-20261101/`),
  the node_modules symlink to the main tree, and the
  pinned-vintage dxvk builds (tools/dxvk/build.w64,
  build.w32) plus gstreamer payloads
  (tools/rootfs64/gstreamer64, gstreamer32) copied from
  wt-fd38.
