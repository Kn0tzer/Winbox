# Parallel browser probes

## The old world (retired)

Probes used an exclusive lockfile, `/tmp/bw64-probe.lock`: if it existed, the
probe aborted with exit code 2. This serialized all probes to one-at-a-time.
That was a workaround, not a requirement — the user asked for it to be fixed.

## The new world: slot-based concurrency

`web/tests/probe-slots.mjs` maintains separate heavy and light pools in the
same directory, `/tmp/bw64-probe-slots/` by default. Each pool has two slots:
heavy uses `slot-0 … slot-1` (`PROBE_SLOTS`), and light uses
`light-0 … light-1` (`PROBE_LIGHT_SLOTS`).

- Heavy probes include `demo-matrix`, `profile-demo`, replay probes, and
  `celeste-boot`. Heavy is the default for direct `acquireProbeSlot()` calls.
- Suite browser tests use the light pool by default through `ProbeTest`.
- Each pool has its own FIFO queue. Acquisition polls every 2 seconds; dead
  waiters and dead slot owners are removed automatically.
- The default wait timeout is 4 hours (`PROBE_SLOT_TIMEOUT_MS`). Retry loops
  around slot acquisition are unnecessary. Callers can still pass
  `acquireProbeSlot({ timeoutMs })` for a shorter explicit limit.
- Slot files contain only the owner PID. `info-slot-*.json` and
  `info-light-*.json` sidecars record the owner, worktree, arguments, start
  time, and `DEMO`/`JIT` values. Sidecars are removed with their slots.
- Run `node web/tests/probe-slots.mjs status` to list holders and waiters for
  both pools, including owner liveness and age.
- A live legacy `/tmp/bw64-probe.lock` still blocks both kinds. New probes do
  not create that legacy lock. `tools/bw64-build.sh` checks only heavy
  `slot-*` files, so light suite tests do not block builds.

Usage in a probe:

```js
import { acquireProbeSlot } from './probe-slots.mjs';
const __probeSlot = await acquireProbeSlot();
try {
  // ... probe body ...
} finally {
  await __probeSlot.release();
}
```

All 22 lock-using probe scripts in `web/tests/` were migrated to this helper.

## Why separate heavy and light slots

Long game, profile, and replay probes hold a heavy slot for many minutes. The
suite browser tests use light slots, so they can continue while heavy probes
run. Each pool defaults to two slots and can be sized independently with
`PROBE_SLOTS` and `PROBE_LIGHT_SLOTS`.

FIFO queues give each waiter its turn as a slot becomes free. The four-hour
default wait avoids wrapping probes in retries. Slot metadata sidecars make it
possible to identify the worktree and command behind a holder; use
`node web/tests/probe-slots.mjs status` to inspect both queues and their
holders. The build guard reads only heavy `slot-*` files, so light suite tests
do not prevent a build from starting.

## Verification

- 2× concurrent `novideo` boot probes → both `READY`, no crashes.
- 2× concurrent SwiftShader (640×480, WebGPU) boot probes → both `READY`,
  `navigator.gpu` present, no crashes.
- Slot fairness: a third probe waits and acquires a slot as soon as one frees.
- Crash safety: killed probe's slot file is reclaimed by the next acquirer.

## Troubleshooting

- Probe hangs in `acquireProbeSlot`: all slots are legitimately busy, or a
  stale slot file names a pid that `kill(pid, 0)` still sees (e.g. pid reuse).
  Inspect `/tmp/bw64-probe-slots/` and `ps` the pids inside.
- `timed out waiting for a probe slot`: raise the timeout via
  `acquireProbeSlot({ timeoutMs })` or free a stuck probe.
- Early-boot flakiness under concurrency (the original complaint): was CPU
  contention between two SwiftShader instances on 4 CPUs, not a correctness
  bug. Mitigations that work: keep graphics probes at ≤2, use small viewports
  for smoke tests, stagger launches.
