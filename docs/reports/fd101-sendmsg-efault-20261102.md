# FD-101 — the 64-bit sendmsg gather answers for unreadable guest buffers

Register: FD-101 carried verbatim from `lane-fd97`'s register
(tip `ee8a0d261`), closed here as CL-52 (provisional).
FD-102, FD-103 and FD-104 filed.

## The defect

`KMemory64::memcpyFromGuest` zero-fills its whole destination
when any part of the guest range fails its access check, and
`sys_sendmsg64` (`source/kernel/syscall64.cpp`) gathered each
iovec segment into its per-thread scratch buffer through it
with no readability check: a segment pointing at unmapped or
unreadable guest memory was sent as that many zero bytes and
the call reported success. `sendmmsg` loops `sys_sendmsg64`
and inherited it. The same unchecked import applied to the
message's other guest structures — the msghdr itself, the
socket name, the iov array, the control area and the
SCM_RIGHTS descriptor array were all read through accessors
that report an unreadable range as zeros, so each of those
faults was silently absorbed too (an unreadable msghdr became
a successful empty send; an unreadable rights array passed
fabricated descriptor numbers to the wineserver path).

## Host contract, measured first

`test-results/fd101/hostref-sendmsg-fd101.c` on the box's host
kernel (6.17.0-1020-oracle), AF_UNIX socketpairs in all three
socket types:

- sendmsg is all-or-nothing for STREAM, DGRAM and SEQPACKET:
  if any byte of any nonzero segment is unreadable the call
  answers -EFAULT and nothing arrives. This covers a fully
  unmapped segment, a PROT_NONE segment, good+bad and bad+good
  segment orders, and — the case that refines the filed
  entry's fix shape — a segment that starts readable and
  crosses into an unmapped page (96 of 200 bytes readable:
  still -EFAULT, no short-count prefix; the socket send does
  not share the write family's prefix contract measured for
  FD-97).
- Zero-length segments are skipped even with unreadable bases
  (the send succeeds). A PROT_WRITE-only segment sends its
  real bytes (x86 write-implies-read, as FD-97 found).
- Import order: the descriptor answers precede the copy
  faults — a bad fd with a bad segment answers -EBADF, a
  regular-file fd -ENOTSOCK (88), for good segments too. The
  msghdr itself unreadable answers -EFAULT, as do an
  unreadable iov array, an unreadable msg_name (even on a
  connected socket, and on an unconnected datagram socket),
  an unreadable msg_control, and an SCM_RIGHTS descriptor
  array crossing into an unmapped page. An iovlen of 1025
  answers -EMSGSIZE (90), not -EINVAL. sendmsg with iovlen 0
  returns 0.
- sendmmsg: a good message before a bad one is delivered and
  its count (1) is returned with the first message's msg_len
  written back; a bad first message answers -EFAULT with
  nothing delivered; a message whose segment crosses a page
  boundary answers -EFAULT with its msg_len left 0.
- Companions: sendto (`hostref-sendto-fd101.c`) is likewise
  all-or-nothing -EFAULT for unmapped and crossing buffers on
  stream and datagram sockets, with -ENOTSOCK preceding the
  fault on a file descriptor — filed as FD-103. recvmsg
  (`hostref-recvmsg-fd101.c`) answers -EFAULT for an
  unwritable receive buffer; on a stream socket the data
  survives the failed call, on a datagram socket the failed
  call consumes the datagram — filed as FD-104.

## The fix (commits `f70b16116`, `4363174fa`)

`sys_sendmsg64` now validates the whole import, in the host's
order, before the gather; every check returns before
`ksendmsg` is reached, so a rejected message sends nothing:

1. The descriptor is resolved first: no descriptor answers
   -EBADF; an object that is neither a native nor a unix
   socket answers -ENOTSOCK — the same tests `ksendmsg`
   itself applies, which today run only after the gather.
2. The msghdr's 56 bytes must be readable, else -EFAULT.
3. The socket name (when present, after the existing
   length answer) must be readable over its full length,
   else -EFAULT. The shared `bounceSockaddrTo32` helper is
   deliberately untouched — its contract serves the other
   socket calls; the check sits in sendmsg's import.
4. An iovlen over 1024 answers -EMSGSIZE; the iov array
   (16 bytes per vector) must be readable, else -EFAULT.
5. The control header (16 bytes, when control is present
   and controllen covers it) must be readable, else
   -EFAULT; inside the SCM_RIGHTS translation, the
   descriptor array must be readable, else -EFAULT.
6. Each segment, after the existing scratch-capacity clamp,
   must be readable over exactly the range the gather will
   copy, else -EFAULT.

The readability test is FD-97's helper
`sys_guestReadablePrefix64` — leading readable bytes counted
page by page via `getPageFlags`, requiring mapped + READ, the
same predicate the copy's own access check applies (write-only
pages carry READ under the x86 rule, so they count as
readable exactly as they do for the host). On this branch the
helper is defined beside its caller, ahead of
`sys_sendmsg64`, so the `socket64-message` JS slice test —
which slices from the helper through the socket functions —
contains it; lane-fd97 defines the identical helper ahead of
`sys_write64`, and the merge keeps one copy. `sendmmsg`
inherits the whole contract through its existing loop:
messages before a bad one are delivered and counted, a bad
first message returns the -EFAULT.

Validation covers exactly the bytes the gather copies: the
per-segment check runs after the pre-existing 64 KB scratch
clamp (the dataCap), so the historical truncation of an
oversized message to the scratch capacity is byte-for-byte
the behaviour the base had — this lane changes only what an
unreadable range does, not how much a readable message
carries.

## Audit of the neighbouring paths

- `sys_sendto64`: the same trait in the single-call socket
  send — it does not share the msghdr gather, so it is filed
  (FD-103) with its host contract measured, not fixed here.
- `sys_recvmsg64`: the mirror direction — it consumes via
  `krecvmsg` into scratch, then scatters through
  `KMemory64::memcpyToGuest`, which returns silently when
  its access check fails, so an unwritable guest segment
  loses received bytes the call reports delivered. Filed as
  FD-104 with the host's stream/datagram consumption
  difference measured.
- The SCM_RIGHTS translation derives its descriptor count
  from the guest-supplied `cmsg_len` with no clamp against
  the header size or `msg_controllen`; a length under 16
  underflows the count to ~2^30 iterations writing past the
  scratch cmsg area. Filed as FD-102 (code evidence); the
  FD-101 rights-array readability check applies only when
  the length at least covers the header, and deliberately
  does not clamp the count.
- The 32-bit message paths run over the 32-bit memory model
  and were outside the filing.

## Proof

- New test `testSendmsgBadBuffer64Syscalls` (x64 suite),
  commit `7eed978a9`: nineteen cases through the real 64-bit
  dispatch — stream and datagram socketpairs, unmapped /
  PROT_NONE / crossing segments, both segment orders, an
  unreadable msghdr, iov array, name, control header and
  rights array, iovlen 1025, the descriptor-order guards
  (-EBADF, -ENOTSOCK), zero-length and write-only segment
  guards, and the three sendmmsg boundary shapes. RED on the
  base (`test-results/fd101/red-x64.log`, the suite's only
  failure): "sendmsg crossing returned 200, peer holds 200
  bytes"; good+bad and bad+good segment orders returned 24;
  an unmapped iov array returned 0; an unmapped msghdr
  returned 0; iovlen 1025 returned 0; sendmmsg good,bad
  returned 2. GREEN on the fix (`green-x64-one.log`).
  Harness note: the peer's received bytes are drained with
  recvmsg gated by a zero-timeout poll — a plain read waits
  on a different queue in the unix-socket object and blocks.
- Full native suites, serial, 1 thread, on the final tree:
  main 668/668 (`green-main4.log`), x64 70/70
  (`green-x64-2.log`), 0 FAILED.
- Browser build of the final tree: BUILD COMPLETE, "Browser
  export verified"; BUILD_INFO git_sha = `4363174fa` (the
  closing commit), wasm `0eb8bb1c` — the helper relocation
  in that commit does not change codegen, so the wasm is
  byte-identical to the build of the fix commit
  (`build-browser3.log`, git_sha `f70b16116`). The first
  export attempt stopped in staging for the generated dxvk
  payloads a fresh tree lacks; they were copied from the
  integration tree as the lane instructions prescribe
  (`build-browser2.log`).
- JS unit: 348 passed, 0 failed (`js-unit2.log`). The first
  run (`js-unit.log`) failed only `socket64-message`: the
  slice test compiles `sys_sendmsg64` against a mock, and
  the fix widened the function's surface (the readable-
  prefix helper, the descriptor lookup, the page flags).
  Commit `4363174fa` defines the helper beside its caller so
  the slice contains it, and extends the mock to model that
  surface (all-readable page flags, a unix-socket
  descriptor, the added errno/type constants); the test's
  assertions are unchanged. The no-silent-failure scanner
  passes unchanged — the new answers are guest-visible
  validation results returned before any work, and its
  baseline does not count them; no bump was needed.

## Boundaries kept

- The gather's scratch behaviour is untouched: same scratch
  layout, same 64 KB data capacity and clamp, same cmsg
  translation for well-formed control data. For fully
  readable messages the call path is the base's, plus the
  validation reads.
- `bounceSockaddrTo32` and `ksendmsg` are unchanged; the
  descriptor tests are duplicated ahead of the gather
  (same constants, same order ksendmsg applies) rather than
  moved, so the wineserver fd-passing hot path keeps its
  own checks exactly as they were.
- FD-102's count clamp, FD-103's sendto validation and
  FD-104's recvmsg validation are filed, not fixed, here.
