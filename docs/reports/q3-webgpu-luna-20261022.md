# Q3 WebGPU and CPU fault report, 2026-10-22/23

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-solq3-20261021`.
Prior rendering findings: [q3-webgpu-sol-20261021.md](q3-webgpu-sol-20261021.md).
Probe evidence: `/tmp/winbox-finish/r3/work-luna-q3/evidence/`.

> **Correction (2026-10-23):** The initial reading of RIP `0x10270` as a
> legitimate Q3 CPU-detection probe was wrong. The Build 5 wild-jump trace
> shows a null indirect call from Q3 followed by unintended low-memory
> execution. See “Root-cause correction and CPU64 fix” below.

## Result

The exception at guest RIP `0x10270` is a valid string-I/O instruction, not an
unknown or misdecoded opcode. The diagnostic runtime logged these bytes:

```text
6d 03 7c 03 4e 02 8c 03 11 02 11 02 11 02 35
```

Opcode `6D` is `INSD`: read a 32-bit value from the port in DX and store it
through ES:[RDI], then advance or retreat RDI according to DF. The emulator
recognized it in its explicit user-mode privileged-instruction path and raised
#GP. The remaining bytes are subsequent instructions, not part of the
one-byte `INSD`.

The fault reproduces with `recover=1` under both probe JIT settings. JIT-off
reports the x64 engine disabled; JIT-on reports the WASM engine enabled
(10,828 blocks emitted and 411,481,226 block entries before capture). Both
runs use copied integration build `a4ffa02c3eee` from commit
`dda8743b1827af5a8843bd5dc0f3a462ac120664`. The diagnostic run used local
runtime build `bcb7f7188717`, with JIT off and recovery on. All three runs
mapped the game, used WebGPU, and reported no page errors or WebGPU failures.

| Probe | JIT | Recovery | Result |
| --- | ---: | ---: | --- |
| `q3-fault-jit0-final` | off | on | #GP at `0x10270` |
| `q3-fault-jit1` | on | on | #GP at `0x10270` |
| `q3-fault-byte-dump` | off | on | #GP at `0x10270`; byte dump above |

The +600 s composited capture still shows the Quake III exception card. The
capture is
[`mapped-plus-600s.png`](/tmp/winbox-finish/r3/work-luna-q3/evidence/q3-fault-byte-dump/mapped-plus-600s.png).
Earlier map-start captures recorded in the prior report show Q3DM1 rendering
through WebGPU behind the loading overlay; the guest does not reach active
gameplay.

## Change and validation

Recovery mode now handles non-REP `INS` word/dword forms by writing zero for
an absent port and updating the destination index using the operand width,
address width, and direction flag. This covers the observed `INSD` and its
operand-size-prefixed `INSW` form. Strict mode still raises #GP for `INSD`;
other privileged I/O forms keep their existing fault behavior.

The CPU64 self-test adds recovery checks for zero-valued `INSD` and `INSW`
writes and pointer updates, plus a strict-mode `INSD` fault check. Native
`x64-cache` compilation succeeded. The native CPU64 self-test passed 909/909
cases, both normally and with `BW64_JIT=1 BW64_JIT_VERIFY=1` (the native
target reports its JIT engine as interpreter fallback).

The browser runtime used for the diagnostic probe was built before this
compatibility change. The WASM build allowance was exhausted, so no post-change
browser run verifies that Q3 advances beyond this instruction. The first
subsequent guest fault, if any, is also unknown. Byte dumping is gated by the
`unimpldump=1` probe option.

## Evidence

- JIT off, recovery on: `/tmp/winbox-finish/r3/work-luna-q3/evidence/q3-fault-jit0-final/`
- JIT on, recovery on: `/tmp/winbox-finish/r3/work-luna-q3/evidence/q3-fault-jit1/`
- Fault-byte diagnostic: `/tmp/winbox-finish/r3/work-luna-q3/evidence/q3-fault-byte-dump/`
- Copied integration export: `/tmp/winbox-finish/r3/work-luna-q3/integration-dist/`

No other worktree was modified. No push, deployment, Docker action, or runtime
asset manifest change was made.

## Build 5 browser verification, 2026-10-23

Build 5 came from integration commit
\`336572461556d787b9ad997d36f88ccfa0430a41\`, built at 02:16 UTC. Its WASM
SHA-256 is
\`3a6bd71f8778c0e1e266844363d8078ce5333fc1f4d6de9a761ad1ec4bb2a5d0\`
(build ID \`3a6bd71f8778\`). I copied \`dist/\` into
\`/tmp/winbox-finish/r3/work-luna-q3/build5-dist/\` without editing the
integration worktree.

The copied export omitted \`shader-cache.mjs\`, which \`shader.mjs\` imports.
The first browser attempt got 404s and fell back to WebGL2, so I stopped it.
For the valid run I copied the matching \`web/shader-cache.mjs\` into the
temporary export. The WASM and integration worktree remained unchanged. The
valid probe reports WebGPU active, no WebGPU failures, and no page errors.

With \`jit=1\`, \`recover=1\`, and \`+map q3dm1\`, the first window mapped after
114.6 seconds. The +60 s capture shows guest console startup; the first SDL
present was recorded 80.5 seconds after mapping (about 195 seconds after
launch). That single 500 ms metrics window reports 2 FPS / 500.06 ms. By +120 s
the composited image shows the Quake logo and “connecting to localhost.”
Later sampled windows report no SDL presents, so this run does not establish a
steady gameplay frame rate.

At +360 s and +480 s, the composited screenshots show a textured Q3DM1 scene
through WebGPU behind “LOADING Q3DM1” / “LOADING... MAPS/Q3DM1.BSP.” The prior
fault at \`0x10270\` did not recur; the guest reached map loading. It did not
reach a playable match. The emulator logged recovery skips at \`0x1029e\`
(\`c5 03 c6 03\`), \`0x1031c\` (\`1f\`), and \`0x1032a\` (\`2f\`), followed by
#GP at \`0x10396\` with bytes \`f4 04 03 05...\` (\`HLT\`). Wine then reported an
invalid SEH frame and could not dispatch the exception. The guest exited with
status \`0xC0000096\`, closing the browser page before the +600 s capture.

I left HLT behavior unchanged. User-mode HLT should fault, and the log shows
earlier recovered opcode skips in the same low-address code region; treating
HLT as a no-op could hide a bad control-flow path. The preceding guest state
and the effect of those skips need tracing before choosing another compatibility
rule.

The shared-load waiter returned \`load 4.67 < 5\`. Evidence is under
\`/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/\`:
[\`+60 s\`](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/mapped-plus-60s.png),
[\`+120 s\`](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/mapped-plus-120s.png),
[\`+360 s\`](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/mapped-plus-360s.png),
[\`+480 s\`](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/mapped-plus-480s.png),
[summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/summary.json),
[console log](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/console.log).

## Root-cause correction and CPU64 fix, 2026-10-23

The focused Build 5 wild-jump trace supersedes the earlier guess that `0x10270` was a Q3 CPU-detection probe. The new trace does not itself record `0x10270`; it does show the chain into the low-address region that preceded the later #GP:

```text
RIP=0x14009ff2e  ff 15 6c 6b fa 00    call qword ptr [rip+0xfa6b6c]
RIP=0x0          00 00 00 00
RIP=0x2          00 00 00 00
recover skipped RIP=0x1029e len=2 bytes=c5 03
recover skipped RIP=0x1031c len=1 bytes=1f
recover skipped RIP=0x1032a len=1 bytes=2f
#GP at RIP=0x10396 bytes=f4 ... (HLT)
```

The call site is in `quake3e.x64.exe`: PE ImageBase `0x140000000`, RVA `0x9ff2e`, inside `.text`. Its RIP-relative operand resolves to VA `0x141046aa0`, RVA `0x1046aa0`, inside the zero-filled virtual tail of the Q3 image's `.data` section. It is outside the PE import directory. The next RIP of zero proves that this callback slot held a null pointer at call time. The reason Q3 reached this internal null callback remains unknown.

The recovery skips and HLT are at `0x103xx`, far outside the Q3 PE image. The trace does not identify that low memory as a Wine/DLL image, and the bytes are not a verified instruction stream from a module. They were executed after the null call, while absent memory reads were being zero-filled and decoded. The earlier `INSD` observation at `0x10270` should likewise be treated as untrusted low-memory execution; this trace does not prove its exact position in the same run. HLT is genuinely privileged in user mode. CPU64 already raises #GP for it and routes the synchronous SIGSEGV through Wine's exception handler. The trace then shows Wine rejecting an invalid SEH frame; whether the runaway itself caused that frame corruption is not proven.

CPU64 now checks each instruction page before interpreter or JIT execution. An unmapped fetch raises #PF/SIGSEGV with `si_addr=RIP`, `CR2=RIP`, and the user instruction-fetch error bits; a mapped non-executable page raises the protection form. The page-permission cache follows mapping generations. `munmap` marks pages unmapped while retaining backing buffers, so the existing Wine-compatible stale data reads remain intact. Recovery no longer turns `INSD`/`INSW` into zero input; both remain #GP faults.

The CPU64 self-tests now cover the null indirect call stopping at its first unmapped fetch, non-executable fetches, permission-cache invalidation after `munmap`, and preservation of #GP for `INSD`, `INSW`, and `HLT` in recovery mode. I did not compile or run these tests because you are scheduling builds. No post-fix browser probe has run; this commit needs to be included in the next scheduled build to verify the Wine SEH path and Q3 progress.

Evidence: [wild-jump console log](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-wildjump-trace/console.log), [wild-jump summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-wildjump-trace/summary.json), [Build 5 Q3 executable in the copied probe archive](/tmp/winbox-finish/r3/work-luna-q3/q3-probe.zip).
