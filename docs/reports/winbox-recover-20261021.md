# Winbox recover report — 2026-10-21

## Policy

`BW64_RECOVER` controls guest-side recovery. Emscripten builds default to on;
native builds default to off so unit tests stay strict. The browser URL parameter
`recover=0|1` sets the runtime environment value. The JS bridge wrapper also
reads `recover=0`; in that mode it rethrows bridge-call exceptions.

## Recovered failures

- A `kpanic` on a guest thread logs and terminates that guest process with an
  abort-style status. Other guest processes keep running. The emulator-wide exit
  remains for failures without guest-thread context and when the killed process
  is the last process or PID 1.
- CPU64 and CPU32 can skip an unknown instruction when the decoder can find its
  length. Unknown instructions are logged once per RIP, capped at 200 logged
  RIPs. A repeated-fault guard allows at most three skips at the same RIP,
  then gives up.
  UD2 and INT3 always retain the SIGILL path. The fallback does not clear an
  arbitrary destination register.
- Selected unsupported socket/IPC calls, futex operations, fcntl/prctl and
  related syscall variants, socket options, and ioctl requests log once and
  return their Linux-style error. Native builds keep the previous strict paths
  unless recovery is enabled explicitly.
- GL and Vulkan bridge exceptions and WebGPU validation errors are counted and
  logged once. Failed shader or pipeline work skips the draw/frame. Runtime page
  errors and unhandled promise rejections are logged without marking the guest
  stopped; the Emscripten abort callback still reports an actual wasm abort.

## Deliberate limits

Memory-map and shared-memory remap behavior is not ignored or changed. Unknown
CPU instructions with an indeterminate length, UD2, INT3, and repeated faults
fall back to SIGILL. Unsupported operations not classified as harmless retain
their existing behavior. A failed GPU device is reported but is not recreated;
failed rendering work is skipped rather than repaired.

## Counters

Call `Module._bw64_recover_stats_json_text()` in the runtime frame to read the
native counters. The Vulkan page tier also includes them in `bwVkStats().recover`.
The JS bridge counters are available through `bwRecoverBridgeStats()` in the
runtime frame.

Example with no recoveries yet:

```json
{"enabled":true,"panics":0,"skippedInstructions":0,"loggedRips":0,"sigillFallbacks":0,"unsupportedCases":0}
```

The JS bridge returns a separate snapshot:

```json
{"enabled":true,"caught":0,"logged":0}
```

## Verification

The `wasm64-mt-jit` build and browser export passed on 2026-10-21. The build
record reports WASM SHA-256
`9c7ec911f01e51c472f5d91a3efe09e46f61f83e0f7cd0ce10bb8a9268e4391a`.
The existing native CPU64 cache binary passed **907 tests, 0 failed**. The R4
Node bridge, GL WebGPU, and Vulkan pipeline unit tests passed **9 tests, 0
failed**.

The requested notepad matrix (`DEMO=notepad JIT=0` and `JIT=1`) and native CPU32
self-test were not run. Load stayed above the required 6.0 threshold, peaking at
14.95, while other lanes held both browser probe slots. The matrix acquires a
slot at `web/tests/demo-matrix.mjs:73`; the CPU32 target would require the native
build listed at `tools/browser/README.md:73`. Those checks remain outstanding.
