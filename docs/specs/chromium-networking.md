# Chromium HTTP through a same-origin relay

Capability: `chromium-loopback-http`. Parent objective: make Chromium usable
inside the existing BoxedWine browser runtime before finishing performance
work and moving to Portal. This extends CP4 in `tasks/plan.md`.

The offline runtime currently rejects every Internet socket in `ksocket`,
including Chromium's ordinary IPv4 TCP sockets. Both direct and relay-backed
HTTP attempts fail with WSAEAFNOSUPPORT before any WebSocket is created.
The existing offline default is an intentional boundary and stays intact.

## Contract and scope

- The runtime URL accepts the additive option `network=1`. Other values leave
  networking disabled. This is fixed during module initialization.
- Before libc reads its environment or SOCKFS mounts, the launcher configures
  one endpoint: `__bw64_tcp` beside the runtime page, on the same origin.
  HTTP uses WS, HTTPS uses WSS. No destination URL, credentials, query string
  or fragment can be supplied to the gateway configuration.
- Only that setup sets the host-only flag `BW64_NETWORK_RELAY=1`. The kernel
  allows IPv4 TCP stream sockets only with the exact flag value. Unix/Netlink
  IPC continues to work in both modes; UDP, IPv6 and other families stay
  outside this capability.
- Guest processes cannot change the host libc environment through their own
  environment. Socket byte streams are transported to the configured fixed
  gateway rather than becoming direct host-port WebSocket URLs.
- The acceptance gateway is a test helper attached to the owned loopback HTTP
  server. It forwards only to that same server, accepts only the matching
  Origin and binary subprotocol, and bounds connections, bytes and lifetime.
  It accepts no user-selected target and exposes no public service.
- The default runtime, archive inputs, persistence, and normal launch APIs keep
  their current behavior. Public Internet proxy hosting/authentication is a
  separate deployment capability, not a dependency for this local check.

## Implementation boundaries

Owners: `source/kernel/ksocket.cpp` for the gate;
`project/emscripten/wine64-launcher.js` for initialization;
`web/tests/suite/unit/browser-network.test.mjs` and the local HTTP probe/helper
for verification. `source/kernel/knativesocket.cpp` owns monitor wakeups: the
browser uses a real local pipe because Emscripten leaves socketpair unimplemented.
The compiled-source wakeup regression must fail on the original source and
prove pipe I/O, shutdown and restart after the fix. The test gateway may use the pinned `ws` development
package; production browser code needs no new dependency or external service.
Follow existing U32/negative-errno C++ and four-space launcher JavaScript:

```js
if (param("vkprof") === "1") ENV["BW64_VKPROF"] = "1";
```

Avoid a general proxy URL setting or a production proxy implementation in
this change. Neither Docker containers nor site homepage entries are involved.

## Ordered tasks

1. Preserve the original Chromium failures and prove the kernel gate with a
   compiled-source regression before changing it.
2. Add the launcher configuration and kernel opt-in, with unit checks for
   fail-closed defaults, exact enable value, same-origin URL construction,
   supported socket types and existing IPC.
3. Attach the restricted loopback relay to the probe HTTP server. Verify that
   unrelated Origins and target override attempts are rejected before any
   TCP forwarding. Use one authoritative dependency lockfile.
4. Build only through `BW64_BUILD_OWNER=chromium-network JOBS=2
   tools/bw64-build.sh`, with no live probe. Run the reduced socket/HTTP check
   and real Chromium HTTP render/click/type/checksum acceptance.
5. Run `pnpm test`, `pnpm typecheck` and default offline Chromium acceptance;
   document limits and commit the verified change locally.

## Acceptance and evidence

The old runtime must fail the new enabled-TCP regression and the real HTTP
navigation check. The new runtime must preserve default socket denial and
successful offline Chromium behavior. With explicit networking enabled, the
loopback HTTP server must observe Chromium 109's Windows user agent and serve
the actual fixture; guest JavaScript, readable pixels, one click, exact typed
text and both benchmark results must pass. A connected WebSocket alone is
insufficient. Preserve hashes, logs and screenshots under distinct ignored
`test-results/` tags. Keep failed diagnostics, including both pre-change HTTP
runs, rather than overwriting them.

No user-specific authentication, remote destination, or deployment choice is
required for the loopback fixture. A wider relay would need its own explicit
contract, authorization and abuse checks before use.


## Result

Complete. See `docs/reports/chromium-networking-20261017.md` for the original
failures, expanded native-socket boundary fixes, red/green controls, runtime
hashes, actual HTTP/offline acceptance and remaining limits. Network changes
are additive; the gateway remains a loopback test helper.
