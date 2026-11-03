# FD-102/FD-103/FD-104 — the rest of the 64-bit message fault family: cmsg_len trusted, sendto's fabricated zeros, recvmsg's silent scatter drop

Lane `lane-fd102-104`, 2026-11-02, from `lane-fd101` tip
`4ac6d024e`. Test commit `dcd1e3a8a`; fix commit `9fb96348d`;
slice-surface commit `9b6c993d9`. Host reference programs and
outputs in `test-results/fd102-104/` (host kernel
6.17.0-1020-oracle).

## FD-102 — the SCM_RIGHTS translation trusts cmsg_len

`sys_sendmsg64` derived the descriptor count from the guest's
first control header as (cmsg_len − 16) / 4 with no bound: a
length under the 16-byte header underflows the unsigned
subtraction toward ~2^30 descriptor records written past the
scratch control area, and an oversized length overruns the
448-byte area into the gathered payload itself. Host reference
(`hostref-cmsglen-fd102.c`): cmsg_len 0/8/12/15 answers
−EINVAL; a cmsg_len larger than msg_controllen answers −EINVAL
(24 over 20, 4096 over 20, 2^32 over 20); unaligned lengths
truncate the count exactly as the emulator's division does
(18 → 0 descriptors, 22 → 1); 253 descriptors (the host's
SCM_MAX_FD) are accepted, 254 answers −EINVAL; and the cmsg
validation precedes any data-segment fault (cmsg_len 8 with an
unmapped data segment still answers −EINVAL).

The fix validates the length where FD-101 already checks the
control header's readability — before the gather, in the host's
order: under the header size or past msg_controllen answers
−EINVAL, as does a count over 253. Accepted messages translate
into a dedicated extension area past the existing 64 KB
scratch message (the scratch mapping grows to hold 253
records), so a full-size rights message no longer overwrites
the data area; the receive side's existing 28-record
translation bound is unchanged (a 253-descriptor message
arrives as 28 descriptors with MSG_CTRUNC, which the new test
pins). One measured divergence is filed, not fixed (FD-106):
the host walks the whole control chain, so trailing slack
after a valid first header answers −EINVAL; the emulator
translates the first header only.

RED on the base (`test-results/fd102-104/red-x64-v2.log`):
cmsg_len 20 over controllen 16 sent its descriptor; 24 over
20 sent two; 4096 over 20 sent a message; the 253-record
translation corrupted the payload it shared the scratch with;
254 descriptors sent; a bad length with a bad segment answered
−EFAULT instead of −EINVAL; and an under-header length escaped
the scratch and killed the case outright.

## FD-103 — sendto sends fabricated zeros

The 64-bit sendto dispatcher branch chunk-copied its buffer
through the zero-filling `memcpyFromGuest` with no readability
check (there is no separate sys_sendto64 function; the entry
text names one, the code is the dispatcher's sendto/recvfrom
case block). The fix validates in the branch, ahead of the
copy: the descriptor answers first (−EBADF, −ENOTSOCK —
measured to precede the fault by the FD-101 lane), then the
whole clamped range (the existing giant-message cap applies
first, so the pre-existing truncation of oversized sends is
preserved) must be readable, else −EFAULT and nothing is sent.
Host contract (FD-101 lane's measurements): unmapped and
crossing buffers answer −EFAULT with nothing delivered, stream
and datagram alike.

RED on the base: a stream sendto from an unmapped buffer
delivered 16 fabricated bytes and reported success; a crossing
buffer delivered 200; a datagram sendto from an unmapped
buffer reported 16 sent. Writing the test also measured a
separate object-layer behaviour, filed as FD-108: a datagram
sent with sendto on a connected unix socketpair reaches
neither the peer's message queue nor its stream buffer in any
form poll/recvmsg/read can see (the object's sendto routes to
its stream write path); the test therefore pins the datagram
case on the dispatcher's answer, which is FD-103's claim, and
queues its recvmsg datagrams with sendmsg, which queues real
datagrams.

## FD-104 — recvmsg's scatter drops received bytes silently

`sys_recvmsg64` received into scratch through the socket
object and scattered through `memcpyToGuest`, whose gate
failure returns silently: the call reported every byte
delivered while ranges the emulator cannot write (unmapped,
or restricted by a guest mprotect) lost theirs. Host reference
(`hostref-recvmsg2-fd104.c`, extending the FD-101 lane's
companion): an unwritable receive buffer answers −EFAULT; on
a stream socket the data survives the failed call; a datagram
is consumed by it; a bad range the received bytes never reach
does not fault (a 40-byte datagram into [good 50][bad 50]
succeeds); MSG_PEEK faults the same way and consumes nothing;
an iovlen of 1025 answers −EMSGSIZE; an unreadable msghdr or
iov array answers −EFAULT.

The fix has two halves. Import validation, in the host's
order: the descriptor answers (−EBADF, −ENOTSOCK), the 56-byte
msghdr readable, iovlen over 1024 answers −EMSGSIZE (this also
bounds a guest-controlled walk over the iov array the old code
ran with no cap), and the iov array itself readable. Then the
scatter itself validates as it copies: each segment's
delivered bytes are copied page-piece by page-piece against
the same writability predicate the copy gate uses, and the
first denied piece answers −EFAULT with everything before it
delivered — matching the host's buffer contents exactly,
including a crossing segment's writable prefix. The scatter
runs before the name/flags writebacks, so a faulted receive
writes nothing else back. Validating the ranges before the
receive instead (the filing's suggested shape) is wrong
against the measurement: the host faults only on ranges the
received bytes actually reach, so validation has to happen
with the received count known — during the scatter.

Two boundaries are settled and recorded rather than papered
over. (1) Consumption on stream sockets: the host leaves the
data queued after a faulted scatter; the emulator cannot —
the socket object consumes into scratch with no pushback, the
arriving amount is unknowable before the receive, and the
object implements no MSG_PEEK to probe with. The answer
(−EFAULT) and the delivered prefix match the host; datagram
consumption matches exactly; only the stream queue's survival
diverges. (2) A receive buffer on a page the guest mapped
read-only from birth: the host answers −EFAULT, the emulator
delivers — its memory model treats such pages as
commit-on-write (the write is denied only for unmapped pages
and pages a guest mprotect restricted), the same predicate
FD-97's write family settled on. The new test pins this
boundary explicitly so a future memory-model change sees it.

RED on the base (`test-results/fd102-104/red-recvmsg-v2.log`):
a stream receive into an unmapped segment reported success;
[good][bad] reported 100 delivered; a crossing segment
reported 200; the datagram forms reported the same; a
guest-restricted segment reported success; iovlen 1025 was not
−EMSGSIZE; an unmapped iov array and an unreadable msghdr
were not −EFAULT; a good+bad recvmmsg pair reported 2. The
guards — short datagram, read-only-born boundary, bad-fd
ordering, ordinary receive — passed on the base and still
pass. (One harness note: the unreadable-msghdr case uses a
guest-restricted page rather than a never-mapped address —
scalar reads of a never-mapped header in this harness return
stale table contents, and the base's unbounded walk spins on
the garbage fields, verified with gdb; the restricted page is
the same contract with a base that stays responsive enough to
record its answer.)

FD-104's receive also exposed its siblings: recvfrom scatters
through the same silent accessor in the same dispatch case —
filed as FD-105 with its own host measurements (host:
−EFAULT, stream survives, datagram consumed) — and the
recvmsg name copyback goes through the same accessor after a
successful receive — filed as FD-107.

## Proof

- New x64 tests `testSendmsgCmsgLen64Syscalls`,
  `testSendtoBadBuffer64Syscalls`,
  `testRecvmsgBadBuffer64Syscalls`: RED on the base as quoted
  above, GREEN on the fix.
- Full native suites serial, one worker: main 668/668, x64
  73/73, 0 FAILED.
- Browser build: BUILD COMPLETE, "Browser export verified",
  BUILD_INFO git_sha `9b6c993d9`, wasm `4180f7cd`
  (sha256 4180f7cda18d132b84dcfd3aec07931591f744bdad333e283054e11a241d24dc).
  The build's first attempt failed at the pinned-asset
  download (the release URL answers 404); the assets were
  restored from the box's local archive
  (`/home/ubuntu/backups/winbox-runtime-assets-20261101/`)
  through the bootstrap's --local path and verified against
  their pins, after which the build completed.
- JS unit: 348 passed / 0 failed (the socket64-message slice
  mock extended for the new validation surface: the
  big-control-area constant and the write/enforced page
  flags; the silent-failure scanner's reviewed
  unlogged-errno allowance for syscall64.cpp moves 115 → 117
  for the new host-ordered rejection sites, which return
  before any state exists to log about — matching the FD-101
  precedent in the same functions).
- Filed from this lane: FD-105 (recvfrom scatter), FD-106
  (control chain past the first header unvalidated), FD-107
  (recvmsg name copyback), FD-108 (datagram sendto never
  delivered). Closed: FD-102 (CL-53), FD-103 (CL-54), FD-104
  (CL-55) — provisional numbers, as every lane since CL-40;
  the merge audit settles them.
