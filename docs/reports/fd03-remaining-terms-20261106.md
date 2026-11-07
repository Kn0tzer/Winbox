# FD-03 remaining terms (a) OpenCL/CUDA and (b) AVX/AVX2 — examined to closure

Branch `lane-fd03`, base integ `faf95d26c`. No product code changed:
this lane's work is an evidence audit of the two FD-03 terms the
2026-11-05 machine-definition lane left open, conducted at the
integration tip, plus the register closure that evidence supports.

## The question

FD-03's floors (c) machine definition and (d) Vulkan heap were
raised and proven on 2026-11-05 (record:
`docs/reports/fd03-machine-definition-20261105.md`). Two terms
stayed open:

- (a) no OpenCL ICD exists behind the staged `opencl.dll`, and
  there is no CUDA at all, so GPU-compute programs have no API
  to call;
- (b) the CPU advertises at most SSE4.2 — no AVX/AVX2 executes.

For each term this lane either delivers it with proof or proves
it cannot be delivered in the browser target, with the evidence
recorded. Verdict: both terms are proven not deliverable, at
every layer they would have to exist at. FD-03 is closed — see
CL-134.

## (a) OpenCL/CUDA — the chain exists except for the one link that matters

Audited against the shipped set this base pins
(`runtime-assets-20261106`, extracted from the retained archive
`backups/winbox-runtime-assets-20261106/`):

1. The API surface a guest sees is a loader, not an
   implementation. `wine64.zip` stages Wine's thunk pair
   `usr/lib/x86_64-linux-gnu/wine/x86_64-windows/opencl.dll`
   (291,287 B) and `x86_64-unix/opencl.so` (31,560 B);
   `wine32.zip` stages the 32-bit pair the same way.
2. The unix half is a forwarder: `readelf -d` on the 64-bit
   `opencl.so` shows `NEEDED ntdll.so`, `NEEDED libOpenCL.so.1`,
   `NEEDED libc.so.6` — it calls into a host ICD loader.
3. The loader is present and alone: `glibc-rootfs64.zip`
   carries `lib/x86_64-linux-gnu/libOpenCL.so.1` (69,136 B),
   the Khronos ICD loader. Behind it there is nothing: no
   `.icd` file and no `vendors` entry appears in the listings
   of `wine64.zip`, `wine32.zip`, `glibc-rootfs64.zip`, or
   `glibc-rootfs32.zip`, and neither `prefix64.zip` nor
   `prefix32.zip` carries a Khronos OpenCL vendor key in
   `system.reg` (prefix32's only Khronos key is the *Vulkan*
   Drivers key — the graphics bridge, a different API).
   A Khronos loader with zero registered ICDs answers
   `clGetPlatformIDs` with `CL_PLATFORM_NOT_FOUND_KHR`:
   there is no platform to enumerate, no device to open,
   no context to create.
4. CUDA is absent one layer earlier still: no entry matching
   `nvcuda` or `cudart` exists in either wine zip — not even
   a loader is staged.
5. Nothing in the emulator could back an ICD if one were
   registered: `grep -rli opencl source/` matches no file.
   The guest's only GPU path is the Vulkan→WebGPU bridge,
   and WebGPU exposes no OpenCL or CUDA surface an ICD
   could be written against.

So (a) is not "an ICD has not been written yet" in the
ordinary sense: every link of the call chain is present
except the implementation link, and the target environment
offers nothing to build that link against. What would reopen
this term: a browser-exposed compute API with an OpenCL- or
CUDA-shaped surface, or a from-scratch OpenCL implementation
over WebGPU compute — a new subsystem, not a fix to this
defect.

## (b) AVX/AVX2 — every prerequisite is absent, and the negative is pinned

Audited in the source at `faf95d26c`:

1. **No register state.** The only SIMD state in the CPU is
   128-bit: `source/emulation/cpu/common/cpu.h:184` defines
   `union SSE { simde__m128 ps; simde__m128d pd; simde__m128i
   pi; }`, and `cpu.h:253` holds it as `ALIGN(SSE xmm[8], 16)`.
   No 256-bit register type exists anywhere in the tree
   (`grep -rni ymm source/emulation/cpu` finds only comments,
   including `jit/jitSSE.cpp:876`, which notes 256-bit chunks
   with YMM registers were not done).
2. **No state machinery.** CPUID leaf 1
   (`source/emulation/cpu/common/cpu.cpp:449-452`) sets ECX
   bits 0, 9, 19, 20, 23 (SSE3, SSSE3, SSE4.1, SSE4.2,
   POPCNT) — OSXSAVE (bit 27) and AVX (bit 28) are clear.
   CPUID leaf 7 (`cpu.cpp:475-481`) returns all zeros, so
   AVX2 (EBX bit 5) is clear. CPUID leaf 0xD has no case and
   falls to the default (zeros). The one piece of XSAVE
   machinery that exists is hardwired to the legacy mask:
   XGETBV (`cpu64.cpp:5164` and its block) returns
   `EDX:EAX = 0:0x3` — "We advertise legacy XSAVE state
   only: bit0=x87, bit1=SSE" — and XSETBV (0F 01 D1) is not
   implemented at all, falling through to the panic for
   unimplemented 0F 01 forms, so a guest cannot even ask to
   enable the YMM state component. The XSAVE table entry
   itself (`decoder.cpp:1017`) is sized `512*8` — the
   512-byte legacy FXSAVE area — with the comment
   ":TODO: just a guess for the size".
3. **No decoding.** `grep -i vex source/emulation/cpu`
   (all `.cpp`/`.h`/`.inc`) matches nothing, and the decoder
   has no VEX (0xC4/0xC5) entries: neither the interpreter
   nor the JIT can execute a single VEX-encoded instruction,
   so the first AVX instruction a guest issued would fault
   even if the CPUID bit were set — which is why the bit
   must stay clear.
4. **The negative is pinned, not just observed.** The guest
   fixture `tools/testdata/machine_definition_check.c:132-133`
   fails if CPUID ever advertises AVX ("CPUID does NOT
   advertise AVX (not implemented -- must stay clear)"), and
   the native self-test `cpu64SelfTest.cpp:3901` (Test 66)
   pins XGETBV(0) at the legacy mask 0x3.

Delivering (b) would require a new CPU subsystem — 256-bit
register state, XSAVE/XRSTOR with the extended area, XSETBV,
and VEX decoding in both engines, proven family by family —
not a fix to this defect. What would reopen this term:
that subsystem landing, at which point the fixture's pin
flips from "must stay clear" to a positive advertisement
check.

## Checks

Submitted through the checking queue (`submit-check lane-fd03`)
at this lane's base tip `faf95d26c`; the pipeline's raw logs
are the preserved evidence. PIPELINE_SECTION_TO_FILL

## Environment note — the shared store no longer matches this base's pins

`winbox-wt-prep.sh` aborted for this worktree: the shared
store's `wine64.zip`, `wine32.zip`, and `prefix32.zip` no
longer match the pins committed at `faf95d26c` (the store
files carry a 2026-11-07 03:27 stamp; the store is internally
consistent — `sha256sum -c SHA256SUMS` reports all 13 files
OK — so the store was refreshed as a set, and the matching
manifest lives on whichever lane restaged it). This lane did
not touch the store. The audit above therefore reads the
pinned payload set from the retained archive
(`backups/winbox-runtime-assets-20261106/`, whose hashes are
the ones this base commits to) and needs no browser build;
the queue's own checkout is unaffected. The coordinator
should reconcile store vs pins when the restaging lane
lands.
