# Failure logging and recovery patterns

Established by the cleanup/logging/recovery campaign's first lane
(2026-10-30, branch `lane-cleanlog`). Every following wave lane follows
this document. It supersedes the 2026-10-08 `LOGGING.md`, whose file no
longer exists in the tree; the `bwlog` API itself is documented in
`include/bwlog.h`.

## Why this exists

The dead-start regression (commit `a835b2a65`) killed Wine startup with
exactly three guest memory faults and left **no trace anywhere**: the
fault-delivery path logged nothing, so the hunt took days and several
full lanes of bisection. The fix (`692875bd8`) is one commit; the lesson
is structural — a failure that leaves no evidence costs orders of
magnitude more than the failure itself. These patterns make the next
one cheap.

## The one-line rule

Every failure emits one greppable line beginning with the token
`BW64-FAIL`, carrying `key=value` pairs: **what** operation failed,
**where** (subsystem / area), the **values** involved (address, errno,
status, pid), and the **outcome** (retried / fell back / degraded /
gave up / delivered). One line, at the point of failure — not three
calls later, not only in a summary.

Examples (from this lane):

```
BW64-FAIL guest-memfault pid=14 addr=0x7f000048 rip=0x401000 err=7 si_code=2 delivered=1 count=3
BW64-FAIL guest-fetchfault pid=14 rip=0x500000 mapped=1 err=21 si_code=2 delivered=0 count=1
BW64-FAIL fetchBytes url=... label=wine64.zip attempt=1 error=network down; retrying once
BW64-FAIL prefetch url=... range=1048576-1572863 error=range 404; continuing without this prefetch
```

## Which logging API

- **C++ subsystem code:** `BLOG_ERROR` / `BLOG_WARN` (`include/bwlog.h`)
  with the file's subsystem id.
- **C++ legacy / CPU code:** `klog_fmt` (`include/log.h`) — match the
  convention of the file you are in; the `BW64-FAIL` token is what makes
  the line findable, not the API.
- **Unsupported / degraded operation, once per distinct case:**
  `bw64RecoverLogOnce(key, message)`. Reporting through this helper is
  **unconditional** (changed in this lane): only recovery *actions* are
  gated by `bw64RecoverEnabled()`, never the fact that something
  unsupported happened. (Single-line sites of the form
  `if (bw64RecoverEnabled()) LogOnce(...); else kwarn_fmt(...)` are
  loud in both modes by construction and are not a problem; the
  multi-line gated blocks are the census item below.)
- **JS (launcher / web):** `console.warn` for recovered failures,
  `console.error` for give-ups, same `BW64-FAIL` prefix.

## Rate limiting

A failure on a hot path must not flood the log, and must not go silent
either. The established shapes:

- **Counting cap:** log the first 64 occurrences in full, then every
  1024th, always with the cumulative `count=` in the line
  (`CPU64::deliverGuestMemoryFault`, `validateInstructionFetch`).
- **Once per key:** for capability gaps and unsupported operations
  (`bw64RecoverLogOnce`, the launcher's per-URL prefetch failure set).
- **Always:** the severe variant — a failure whose *handling* also
  failed (e.g. fault delivery fell back) logs regardless of the cap.

## The recovery ladder

When an operation fails, take the highest rung that is honestly
available; log the rung you took in the same line.

1. **Retry** — bounded (once is the established default), only for
   transient failures, after a short delay. Never retry a definitive
   answer: `fetchBytes` rethrows `HTTP 404` immediately because the
   caller's recovery is the chunked-parts fallback.
2. **Fall back** — a second way to get the same result (whole-file zip
   → split-parts manifest in `resolveLazyWineSpec`). The fallback
   existing is not a reason to stay silent about the primary failing.
3. **Degrade** — continue with less (range prefetch skips the failed
   block; the session runs without it).
4. **Report and continue** — return the honest error to the guest
   (`ENOSYS`, `ENOTTY`, `EINVAL`) *and* log it once per case. Returning
   an error code is not a failure of the emulator; hiding it is.
   Watch for the inverted form of this rung: a graceful branch that
   already exists but is gated behind a recovery flag, with a `kpanic`
   as the default path. That makes an unsupported guest argument kill
   the whole emulator in the default configuration. Where the graceful
   branch returns exactly what Linux returns for that input, make it
   unconditional and delete the panic (11 syscall sites converted in
   this lane: `ugetrlimit`, `clock_gettime`/`64`, `prlimit64`,
   `modify_ldt`, `clock_nanosleep`, `sigprocmask`, `sigtimedwait`,
   socketcall, `ipc`, `vm86`). A panic is for violated internal
   invariants, never for guest input.
5. **Fail loudly and immediately** — when no rung above is honest,
   fail at once with the `BW64-FAIL` line and the reason. Never fail
   silently, and never hang: any wait that can block forever needs a
   bounded watchdog that logs and breaks the wait (the range-prefetch
   stall backstop in the launcher is the model).

Deliberate exception, do not "fix": instruction-skip recovery
(`BW64_RECOVER`) stays opt-in. Silently stepping over an undecoded
opcode turns a hard fault into a wrong-result run; that is a debugging
aid, not a recovery strategy.

## Rules for new and cleaned code

- No empty `catch` / `.catch(() => {})`. If a swallow is genuinely safe
  because another path surfaces the error, the comment must name that
  path (the `zipDownloads[name].catch(...)` site in the launcher is the
  model: it says exactly where the error surfaces).
- No silent early return on an error branch. If the function returns a
  failure value, the failure gets its line first (subject to the rate
  limits above).
- Do not gate *reporting* behind a feature flag, an opt-in env var, or
  a debug level. `ERROR`/`WARN` are on by default; that is the point.
- Log values, not adjectives: addresses, errnos, statuses, counts,
  sizes. "failed" alone is not a log line.
- Preserve working behavior: cleanup never removes a guard, a safety
  check, or error handling that protects data, and a recovery rung
  never changes a success-path result.

## Census: silent paths found by this lane

Fixed here:

| Path | Was | Now |
|---|---|---|
| `CPU64::deliverGuestMemoryFault` (cpu64.cpp) | SIGSEGV delivered, zero logging — the dead-start blind spot | `BW64-FAIL guest-memfault` line, counting cap, delivery failure always logged |
| `CPU64::validateInstructionFetch` (cpu64.cpp) | SIGSEGV delivered silently (optional control trace only, off by default) | `BW64-FAIL guest-fetchfault` line, same shape |
| `bw64RecoverLogOnce` (util/log.cpp) | gated by `bw64RecoverEnabled()`, which defaults off — unsupported-operation reports vanished | reporting unconditional; recovery actions stay gated |
| launcher `fetchBytes` | no retry; one transient failure killed the rootfs load | one bounded retry, per-attempt `BW64-FAIL` lines, 404 still immediate |
| launcher `resolveLazyWineSpec` probe | whole-file probe failure silently fell back to manifest | fallback kept, genuine failure now logs once per call |
| launcher range `prefetch` | `.catch(function () {})` swallowed every failure | first failure per URL logs; prefetch continues degraded |
| knativesocket / knetlink sockopt sites | audited as suspect pre-gated sites | no change: each is an `if/else` pair whose other branch is a `kwarn_fmt`, so the failure was already logged in both modes |
| 11 syscall sites in `ksystem` / `kthread` / `syscall` | graceful `EINVAL`/`ENOSYS` return existed only behind the recovery flag; default path was `kpanic` — an unsupported guest argument killed the emulator | graceful return unconditional, panic deleted, report prints via `bw64RecoverLogOnce` |

Deferred to the wave (verified present, not yet fixed):

- Multi-line `if (bw64RecoverEnabled()) { ... }` blocks across the
  kernel (`knativesocket`, `kprocess`, `ksystem`, `knetlink`,
  `kthread`, `cpu64`): their contents mix reporting with possible
  recovery actions. Audit per site — only the reporting should come
  out from behind the gate; the actions stay gated.
- 32-bit CPU fault delivery (`source/emulation/cpu/x32/` and the
  32-bit syscall layer): not yet audited for the same silent-delivery
  pattern.
- Kernel subsystems generally (`kepoll`, `kinotify`, `kunixsocket`,
  `kprocess`): audit for error branches that return a value with no
  line; `kinotify`'s loud `ENOSYS` stance is the model to match.
- Page-side `web/*.mjs`: audit catches and promise rejections for the
  same patterns as the launcher fixes.
