# Chromium HTTP acceptance — 2026-10-17

Chromium 109 now fetches and uses the existing smoke page through a restricted
loopback HTTP relay. The default launch stays offline. This completes CP4a;
Chromium performance work remains open, and Portal has not been started.

## Runtime contract

`network=1` enables IPv4 TCP streams (protocol 0 or 6). Other values, including
malformed encodings, leave Internet sockets blocked. Unix and Netlink IPC keep
working. UDP and IPv6 are outside this capability. The launcher sets the host
libc environment before initialization and fixes SOCKFS to the same-origin
`__bw64_tcp` endpoint beside the runtime page, using WS or WSS as appropriate.
The endpoint contains no credentials, query, fragment or guest-selected target.

`HTTP_FIXTURE=1 node web/tests/chromium-probe.mjs` attaches a test-only relay to
its owned loopback HTTP server. Every TCP stream reaches that same server;
there is no arbitrary destination option. It requires the matching browser
Origin and binary subprotocol, limits payload/connection sizes and lifetime,
applies backpressure, and destroys rejected half-open connections. Origin
checks constrain browser clients; they are not authentication for native
programs. No public proxy, DNS, remote HTTPS or deployment was added.
The only new package is the exact development dependency `ws@8.22.0`.

## Reproduced failures and fixes

The pre-change direct and relay-backed HTTP runs failed with WSAEAFNOSUPPORT
before creating a WebSocket: `suite-chromium-http-direct` and
`suite-chromium-http-relay`. The compiled kernel regression also failed for
explicitly enabled TCP before the opt-in gate was changed.

After that gate, two accepted WebSockets still carried zero bytes
(`suite-chromium-http-gated` and `suite-chromium-http-pipe`). A reduced WinSock
program fetched HTTP when forced to send, but `connect` returned immediate
success and its FD_CONNECT wait timed out. SOCKFS returns zero before its
WebSocket opens. Reporting EINPROGRESS for nonblocking sockets allows Wine
to finish its asynchronous connection and deliver the event. Blocking sockets
wait for native completion instead; pending and established state are separate.
See the [Wine connection path](https://github.com/wine-mirror/wine/blob/wine-8.0/server/sock.c#L2538).

The socket monitor also used Emscripten's unimplemented socketpair syscall.
The browser now uses a real local pipe, checks its select descriptor limit,
and releases it after shutdown. Monitor stop state is atomic, descriptor
users follow lifecycle-before-list lock order, notification lists own their
condition references, and one poller's cancellation preserves other parents.
`KSystem::destroy` excludes further guest execution before stopping the monitor;
thread-table removal is not a join of every detached host thread.

Review exposed additional native-path defects. Socket creation failures and
resource errors now propagate without publishing invalid descriptors; SO_ERROR
reads native asynchronous errors. `sendmsg` snapshots bounded descriptors once
and checks totals/address arithmetic before allocation/copying. `recvmsg`
returns after a partial vector or peek, preserving contiguous stream positions.

Compiled-source regressions fail on the original source for the wakeup pipe,
send descriptor race, condition use-after-free (ASan), and lost subscriptions.
Negative controls restoring the remaining boundary defects fail descriptor
limit/exhaustion, scatter-read and pending-connect assertions. All corrected
checks pass. Three fresh read-only review cycles informed these fixes; no
separate cross-model review was run. Extracted host checks complement the
actual browser runs and do not establish browser behavior by themselves.

## Final acceptance

Runtime WASM SHA256: `46eb53ee977739bb0f765dc52387e6774ad1aee5252ae31dafbdd07bf49cd30a`
(3,259,685 bytes). Runtime code hash:
`f4a99523b54ce4b2e58fa003aca7d91de6167622f28126662eef0959b508fe62`. The Chromium archive and HTML fixture hashes remain
`6420689eb3793a78d8f1a4b4e785b527fe27e5f657db845f973c29949cd28e71` and
`7008723cbab78d48b02805070fcea0f45046f9f90ebce125f69589a9379581d0`.

- `suite-chromium-network-final-http`: guest Windows Chrome 109 GET observed;
  1,229 bytes sent and 5,258 received through the fixed relay.
  One Count click, exact `boxedwine works`, checksum 1783293664, and canvas hash
  1470154170 with 7,200 colorful pixels all pass. Readiness: 31.299 s.
- `suite-chromium-network-final-offline`: the same interaction/checksums pass
  without enabling networking. Readiness: 27.637 s.
- `suite-winsock-http`: nonblocking connect returns WSAEWOULDBLOCK, FD_CONNECT
  arrives before any send, and the HTTP response body matches.
- `suite-cpu64-selftest`: all 327 actual browser CPU64 assertions pass.
- `pnpm test`: legacy checks plus all 127 fast unit groups pass.
  `pnpm typecheck` also passes.

The earlier HTTP render succeeded but the OCR gate failed under uniform-block
segmentation (`suite-chromium-http-async`). Automatic segmentation then passed
HTTP but missed controls in an offline capture (`suite-chromium-network-offline`).
The probe now accepts either mode only when that pass finds all three labels,
preserves its TSV, and uses one OCR thread. It sends one Count click without a
blind retry. Final offline click delivery took 2.834 s;
that latency outlier remains unexplained and is not specific to rejected LTO.
It is not a failed interaction or evidence of improved latency.

Raw red/green and boundary-control logs are under
`/tmp/chromium-performance/network-*.log`; browser summaries, screenshots,
console output and exact runtime metadata remain in their distinct result tags.
