# FD-120..123 — socket state answers: drained reads, unconnected receives, non-listening connects, addressed sends (2026-11-03)

Branch `lane-fd120-123`, based on `lane-fd118-119` tip
`60359b324`. Commits: `a850a3356` (the guest test, RED on
the base, and the FD-119 pin correction), `83c6e147a`
(a test-staging correction: an anchor descriptor),
`49e7c983b` (the fix), `d13bad99b` (the FD-123
reconciliation pin in FD-111's guard), `180e48080`
(the unix-connect-handoff harness completion), plus
the register/record commit carrying this file.

Numbering note: this branch's entry numbers are its
own lineage's. The batch-4 merge was already running
when this lane started, so this branch composes in
batch 5 alongside `lane-fd118-119` and the audit
renumbers there in filing order. Closures below are
CL-71..CL-74 (FD-120..FD-123) provisionally, this
lineage's next after CL-70; the new filing is FD-124
in this lineage. Each defect is therefore also
described by its substance.

All host behaviour was measured first on the box's
kernel (6.17) with small reference programs
(`test-results/fd120-123/hostref-fd120-123.c`,
`hostref-fd120-123b.c`, `hostref-fd120-123c.c`).

## FD-120 — a drained datagram read is not end of stream

Substance: on a datagram pair, after the peer writes
one byte and closes, the survivor receives the byte
and a further nonblocking recv answers -EAGAIN on
every receive path (recv, read, recvfrom, recvmsg);
with nothing ever queued it answers -EAGAIN as well.
A datagram socket has no end of stream. The controls
hold: a seqpacket survivor drains its queued record
and then answers 0, and a stream survivor drains its
byte and then answers 0 — end of stream belongs to
those two types. The emulator returned the queued
byte and then 0 for datagram too, because the pair
destructor's inClosed marking reaches the read wait
loops' end-of-stream return for every socket type.
Fix: the inClosed end-of-stream return in the read,
readNative and recvmsg wait loops exempts datagram,
whose drained read takes the would-block path like
any empty datagram read (nonblocking -EWOULDBLOCK;
a blocking one waits for the next datagram, the
host's shape — the host probe is nonblocking, so
the blocking half is the loop's own standing shape,
not a separate measurement). The FD-119 guest test
pinned the standing 0 with FD-120 as its reference;
its pin is corrected to the host answer.

## FD-121 — a receive on a socket that is not connected answers per type

Substance: a nonblocking receive on a fresh socket
answers -EINVAL (stream), -ENOTCONN (seqpacket),
-EAGAIN (datagram). A listening socket answers the
same as a fresh one of its type (-EINVAL stream,
-ENOTCONN seqpacket). A second pass
(`hostref-fd120-123b.c`) filled a listener's backlog
so a further connect was genuinely in flight
(-EAGAIN), and the connecting stream socket's
recv/read still answered -EINVAL — so no
connecting state needs a carve-out; "not connected"
is one shape per type. The emulator's read and
readNative gates answered -EPIPE for every
non-datagram type, and recvmsg had no gate at all:
it answered -EWOULDBLOCK on a nonblocking socket
(and would wait on a blocking one). Fix: the two
gates answer per type — stream -EINVAL, seqpacket
-ENOTCONN, other types keep the standing -EPIPE —
behind the standing conditions (not inClosed, no
connection, empty queue; a queued message stays
readable input for any socket, per FD-110), and
recvmsg gains the matching entry gate, also
requiring an empty stream buffer. The datagram
exemption is unchanged: its input is its queue,
connected or not.

## FD-122 — connecting to a socket that is not listening is refused at once

Substance: a connect to a bound but not listening
socket answers -ECONNREFUSED at once — stream and
seqpacket destinations, blocking and nonblocking
clients alike (the filing lane's matrix had the
seqpacket and blocking shapes; this lane's probe
covers all four, plus the listening control, which
connects). The emulator queued the client on the
destination's pending list, where only an accept —
which a non-listener never issues — or the
destination's destruction resolved it; a blocking
connect waited, a nonblocking one answered
-EINPROGRESS. Fix: the fresh-connect branch, after
FD-118's type check, answers -ECONNREFUSED when
the destination is not listening and queues
nothing; the guest test proves the second half by
listening afterwards and accepting nothing. The
datagram connect branch is a separate earlier
branch and is untouched (datagram destinations
never listen; FD-111's delivery stands). The
unix-connect-handoff JS harness needed its
standing completion for this change: its stub
gained the `listening` member the connect now
consults, and its staged destination is set
listening — the pending handoff it protects (a
blocking retry completing only at accept, never at
a bare notification) can only be staged against a
listening socket now.

## FD-123 — an addressed send on a connected stream socket answers -EISCONN, and FD-111 reconciled

Substance: a connected stream socket's sendto with
a destination answers -EISCONN; sendmsg with a
header name answers the same; nothing is delivered
to the pair peer or to the named target under
either answer. A survivor whose peer closed
answers the same — the answer follows the
connection state, not the peer's liveness. An
UNCONNECTED stream socket's sendto with a
destination answers -EOPNOTSUPP for both pathname
and abstract destinations, and its sendmsg with a
name answers -EOPNOTSUPP too. That is the
reconciliation FD-123's filing asked for: FD-111's
-EOPNOTSUPP measurement did not record the socket's
state, and it is the host's unconnected answer; its
own guest guard, however, stages a connected
socketpair end, whose host answer is -EISCONN — the
guard's pin is corrected to -EISCONN, and the
unconnected shape is pinned by this lane's test.
The emulator answered -EOPNOTSUPP for every
addressed stream sendto, and sendmsg ignored a
header name entirely: on a connected socket it
delivered the byte to the pair peer, on a survivor
it answered -ENOTCONN, on a never-connected socket
-ENOTCONN. Fix: sendto's addressed branch answers
-EISCONN when the socket is a connected stream
socket (the `connected` flag, which a peer's death
does not clear) and keeps -EOPNOTSUPP otherwise;
sendmsg pre-reads the header for stream sockets,
mirroring the datagram destination pre-read FD-111
added, and answers -EISCONN / -EOPNOTSUPP by the
same flag when a name is present. A header without
a name leaves every standing path untouched, and
FD-118's seqpacket addressed shape (the address is
ignored when connected) is not in either branch.

## FD-124 — filed, not fixed: a peer-closed stream survivor's unnamed sendmsg

The FD-123 host pass measured one more shape in
the same function (`hostref-fd120-123c.c`): a
stream survivor's sendmsg WITHOUT a header name
answers -EPIPE on the host (with MSG_NOSIGNAL;
its sendto without an address and its plain send
answer the same), while the emulator answers
-ENOTCONN, its not-connected branch not
distinguishing never-connected (host -ENOTCONN,
measured in the same pass) from peer-died.
FD-118 gave seqpacket's sendmsg exactly that
distinction; the stream branch was left with the
plain -ENOTCONN. Filed as FD-124 in the register.

## Test staging note

The new guest test keeps one anchor descriptor
open for its whole run: the first socket a test
creates can land on descriptor 0, and the 64-bit
dispatcher's read answers a read on descriptor 0
itself (its host-stdin convention, returning 0),
which masked the socket layer's own read answers
during the first RED run. With the anchor, the RED
run shows the socket layer's answers throughout.

## Verification

- Guest tests: RED on the base
  (`test-results/fd120-123/red-x64.log` — the
  FD-119 test fails only on its corrected FD-120
  pin; the new test fails on exactly the filed
  answers: drained reads 0, unconnected receives
  -EPIPE / recvmsg -EWOULDBLOCK, non-listening
  connects -EINPROGRESS with the queued client
  acceptable after a later listen, connected
  addressed sendto -EOPNOTSUPP and sendmsg
  delivering the byte) and GREEN on the fix
  (`green-x64-pair.log`), both at one worker.
- Native suites serial, one worker: main 668/668
  and x64 88/88, 0 FAILED (`suite-main-green.log`,
  `suite-x64-green.log`; the x64 count is the
  base's 87 plus the one new test; the main count
  is unchanged). The first full x64 run on the fix
  failed only FD-111's guard, on the connected
  pin the reconciliation corrects; the re-run
  after `d13bad99b` is the 88/88 above.
- JS unit on the built tree: 348 passed / 0 failed
  (`js-unit.log`), including the repaired
  unix-connect-handoff; the silent-failure
  allowance for syscall64.cpp stays 120 — no
  counted site added or removed (this lane changes
  no dispatcher file).
- Browser build: BUILD COMPLETE, "Browser export
  verified" (`browser-build.log`); BUILD_INFO
  git_sha `d13bad99b` (the tip the build ran at;
  the one later commit touches only a JS test),
  wasm sha256 `10d65ac0`, staged dependency
  archive `c4dbe35986a5` as pinned.
- Build inputs, as the sibling records describe:
  bootstrap --local from the retained
  runtime-assets-20261101 archive (this branch's
  manifest pin), the node_modules symlink to the
  main tree, and the pinned-vintage dxvk builds
  plus gstreamer payloads copied from wt-fd38.
