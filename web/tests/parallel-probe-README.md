# Parallel browser probes

## The old world (retired)

Probes used an exclusive lockfile, `/tmp/bw64-probe.lock`: if it existed, the
probe aborted with exit code 2. This serialized all probes to one-at-a-time.
That was a workaround, not a requirement — the user asked for it to be fixed.

## The new world: slot-based concurrency

`web/tests/probe-slots.mjs` implements N atomic probe slots (default **2**,
override with `PROBE_SLOTS` env).

- Slots live in `/tmp/bw64-probe-slots/slot-0 … slot-(N-1)`, one file per slot.
- Acquisition is atomic via `O_CREAT|O_EXCL`; a probe waits (polling every 2s,
  default timeout 15 min) for a free slot instead of aborting.
- A slot whose owner pid is dead is reclaimed automatically (crash-safe).
- Backward compatible: if a legacy `/tmp/bw64-probe.lock` is held by a live
  process, new probes wait for it. New probes never create the legacy lock.

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

## Why 2 slots, and when to raise it

The box has 4 CPUs and 23 GB RAM. Constraints per probe class:

| Probe class | Chromium flags | Bottleneck | Safe concurrency |
|---|---|---|---|
| Boot / non-GUI (`novideo=1`) | `--disable-gpu --disable-software-rasterizer --disable-webgl` | wasm compile + Wine boot, ~1 CPU each | 3–4 |
| Graphics (SwiftShader WebGPU) | `--enable-unsafe-swiftshader --use-vulkan=swiftshader …` | SwiftShader software rasterization, ~2–3 CPUs each | 2 |

Rules of thumb:

- **2 concurrent graphics probes**: verified working (see below).
- **3–4 concurrent `novideo` boot probes**: fine; they barely touch the GPU path.
- **Don't mix** 2 graphics probes + boot probes on 4 CPUs and expect full speed;
  the graphics probes will dominate. Stagger graphics-probe launches by ~30s so
  wasm compilation doesn't collide.
- Each probe serves `dist/` from its own HTTP server on a random port (`serve()`
  uses port 0) — no port conflicts. Playwright gives each `chromium.launch()`
  a fresh `--user-data-dir` — no profile conflicts.

Raise slots with `PROBE_SLOTS=4` (boot probes) or keep 2 for graphics work.

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
