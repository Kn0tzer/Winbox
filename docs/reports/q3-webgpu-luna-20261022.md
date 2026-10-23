# Q3 `INSD` diagnosis, 2026-10-22

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-solq3-20261021`.
Prior rendering findings: [q3-webgpu-sol-20261021.md](q3-webgpu-sol-20261021.md).
Probe evidence: `/tmp/winbox-finish/r3/work-luna-q3/evidence/`.

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
