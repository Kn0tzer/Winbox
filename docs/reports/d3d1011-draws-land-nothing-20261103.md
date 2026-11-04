# D3D10/D3D11 draws land nothing (LIT=0) — fixed on branch `lane-d3d11lit`

The defect, by substance: the D3D10 and D3D11 comprehensive
browser tests fail — device creation and shader compilation
succeed, the draw completes without error, and the readback
shows nothing drawn (LIT=0) — while the same builds pass the
D3D12 test. Filed by the FD-125 settlement lane on its own
register (its lineage number FD-127 there, colliding with the
Godot-park FD-127 on the integration register; this lane's
register entry is FD-130, provisional, described by substance
for the merge audit).

## Base

This lane is based on `lane-fd129` tip `ea93a6ef3` (integration
tip `0c7e2f3fa` plus the Godot stack: the FD-127 characterisation
and the FD-127/FD-128/FD-129 fixes), not on the bare integration
tip. Reason, measured: this lane's first reproduction run on the
unmodified integration tip never reached the draw — the guest
froze during DXVK device initialisation with the exact FD-127
signature (NtCreateThreadEx frame, ntdll.dll+0xd5c4, rax=0x2d,
the thread-creation pipe2 never returning; thread dumps at
60 s intervals with noprogress growing to 479,997 ms; console
preserved as `test-results/fd127dxvk/base-integ-fd127freeze-console.log`).
That defect is already filed and fixed on the FD-127 lane; it
bites intermittently, and the settlement's three D3D runs on
similar bases happened to complete. On `ea93a6ef3` the D3D11
defect below reproduced exactly (device OK, LIT=0), and that
tip's build is byte-identical to the FD-129 lane's own recorded
build (wasm sha256 `4453757f0699b527`), so the base is a known
quantity.

## The chain, link by link

1. In the settlement's preserved consoles, each failing
   D3D10/11 run carries exactly one
   `bridge-recover: vkwebgpu.v2-frame-decode: vk64: v2 frame overflowed the guest command limit`
   warning, immediately after the bridge builds the draw's
   frame (`FRAME 0 built (v2): 256x256 draws=1 ... bytes=4460`);
   the passing D3D12 run carries none. The v2 decoder
   (`web/vk-protocol.mjs`, `decodeV2Frame`) rejects any frame
   whose FRAME_BEGIN flags carry bit 0 — a flag the bridge's
   serializer copies from its global frame-overflow state.
   A rejected frame renders nothing, so the readback that the
   submit registered never completes; the next vkQueueSubmit2
   waits the full 10 s on the readback flag and is dropped,
   and the guest maps the staging texture's zeroes: LIT=0.

2. Two louder console signals were investigated and
   exonerated by an instrumented run (temporary DIAG commit
   `561237923`, reverted before the fix; console
   `test-results/fd127dxvk/diag-triangle-console.log`):
   - The two `vkCmdBeginRendering` drop lines per pass are the
     attachment helper invoked with null depth and null
     stencil pointers on a colour-only pass (measured:
     `colors=1 pDepth=0x0 pStencil=0x0 area=256x256`). The
     colour attachment is processed; nothing is dropped.
   - The two `vkCreateGraphicsPipelines` drop lines are
     `captureStageSpec` meeting DXVK's empty
     VkSpecializationInfo (measured: `count=0 dataSize=0
     pEntries=0x0 pData=0x0` for both stages). An empty spec
     info has nothing to capture; the pipeline is created with
     the SPIR-V defaults, which is the faithful outcome.

3. The conviction. In the same instrumented run, the session's
   only `vkCmdClearColorImage` — DXVK's init-image clear of
   the freshly created 256×256 render target (create → bind →
   clear → create views) — measured: image 256×256, 1 mip,
   1 layer, format 37; the range array at pRanges reads back
   as exactly one range (aspect=1, baseMip=0, levels=1,
   baseLayer=0, layers=1), matching the pinned DXVK source,
   whose only clear-color call site passes rangeCount 1. But
   the trap argument slot for the count, `args.a[4]`, held
   `0x200000000001` (35184372088833): the true count in the
   low 32 bits, unrelated bits above. The handler's guard
   compared the full 64-bit slot against its 64-range cap, so
   it fired `vk64FrameOverflow`, which set the global flag
   that poisoned the next submitted frame — the one carrying
   the triangle.

4. Why the guard is the defect, regardless of which layer of
   the guest→bridge marshalling chain left the high bits in
   the slot (that layer was not isolated, and does not need
   to be): the Vulkan parameter is a `uint32_t`; only the
   slot's low 32 bits are the value. The bridge knows this
   everywhere else — 245 `(U32)args.a[...]` truncations in
   the same file, including the `readArray` on the very next
   line of this handler and both `readArray`s in the
   clear-attachments handler whose guards had the same flaw.
   The frame-overflow guards were the only consumers reading
   a 32-bit count as a raw 64-bit slot. (The guest shim is
   not the source: `libvk64.c` zero-extends the count into
   its args struct.)

## The fix

Commit `738d882ca` (`source/vulkan/vk64bridge.cpp` only):
the frame-overflow count guards in `vkCmdClearColorImage`,
`vkCmdClearDepthStencilImage` and `vkCmdClearAttachments` —
all six sites, both ABIs — now compare the Vulkan-typed
value, `(U32)args.a[...]`, against their caps, with a comment
at the convicted site recording the measurement. Nothing
else changes: the caps (64 ranges, 8 attachments, 4096
rects) and the loud overflow response for genuinely over-cap
counts are untouched — they remain the bridge's defence
against absurd counts read from guest memory.

## Proof

Base build (`ea93a6ef3`, wasm `4453757f0699b527`, dirty 0):
- d3d11-triangle FAILS: `D3D11CreateDevice OK`, shaders
  compiled, the v2-overflow warning, LIT=0, RESULT 1, and the
  five-drop chain (2 begin-rendering + 2 spec-capture +
  1 readback submit). Console:
  `test-results/fd127dxvk/base129-triangle-console.log`.
  (The settlement's A/B had already shown all three tests
  failing identically on two earlier builds; its consoles are
  in `wt-fd125ab/test-results/`.)

Fixed build (`738d882ca`, wasm `d3937eb9`, dirty 0,
"Browser export verified", archive `c4dbe35986a5`):
- d3d11-triangle PASSES: LIT=8192, RESULT 0.
- d3d11-comprehensive PASSES: the same 4460-byte FRAME 0 now
  renders; LIT=8192.
- d3d10-comprehensive PASSES: `tri10: LIT=8192`, RESULT 0.
- d3d12-comprehensive PASSES (control; its route never used
  the clear-image guards).
- On the fixed build the only remaining drop lines are the
  four exonerated no-op-shape lines of item 2 above — filed
  as FD-131 (below).
- Native suites, serial, on the fixed tree: main 675/675 and
  x64 95/95, 0 FAILED (logs `native-main-fix-serial.log`,
  `native-x64-fix-serial.log`). JS unit suite: 352 passed /
  0 failed; legacy suite: 115 pass / 0 fail.
- Known-gaps audit: 1909 files scanned, no undeclared gaps
  (every hit owned or allowlisted); the fix adds no new gap
  markers.

## Filed from this lane

FD-131 (this lineage's provisional number): the bridge's
drop logging reports two legal no-op shapes as dropped guest
commands — the dynamic-rendering attachment helper invoked
unconditionally with null depth/stencil pointers, and the
specialization capture declining an empty
VkSpecializationInfo under the vkCreateGraphicsPipelines
drop line. Both are behaviourally faithful; the harm is
diagnostic — in this defect's consoles they read as
render-pass and pipeline loss while the single real signal
was one bridge-recover warning. Closing terms are in the
register entry.

## Evidence

`test-results/fd127dxvk/` in this worktree: build logs for
the base, DIAG and fixed builds; run logs and preserved
consoles for the base (integ-tip FD-127 freeze; fd129-base
LIT=0), DIAG, and fixed runs of all four browser tests;
native suite and JS suite logs.
