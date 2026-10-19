# Portal stencil rendering

Capability: `portal-stencil`. Depends on the existing per-attachment colour blend
repair (`3edaffa6e`, `5b2b44c10`). Portal remains ahead of Gang Beasts; optimization
follows functional acceptance of both. The user has authorized program support
and deploying the resulting fixes. This work changes no product preferences.

Assumptions: Portal uses standard Vulkan depth/stencil state through DXVK;
its stencil views use D24S8 and ordinary 2D attachment clears; its active faces
share reference/read/write masks, as D3D9 normally requires. Unsupported states
must be reported rather than silently rendered with stencil disabled.

The observed defect is portal-view geometry/compositing across the surrounding
image, alongside the separately repaired white projected-shadow rectangles.
The bridge currently discards stencil pipeline state and dynamic setters, uses
depth-only WebGPU targets, and does not replay attachment clears. Success means
the portal view is confined to the opening, surrounding geometry stays intact,
projected shadows have no white pads, and the player can traverse the portal.

## Model and interfaces

- Preserve each face's Vulkan fail/pass/depth-fail operations, compare function,
  compare mask, write mask and reference; preserve stencil enable and the core
  dynamic-state flags. A bounded `STN1` PIPELINE tail follows `BND1`.
- Append shim trap IDs 900–903 for blend constants and stencil compare mask,
  write mask and reference. Existing IDs remain unchanged. Both guest ABIs
  record these commands in order; only pipelines declaring dynamic state use it.
- Add ordered V2 commands for stencil state, blend constants and attachment
  clears. Keep records bounded. Old frames remain readable.
- Preserve depth/stencil formats, stencil load/store operations and clear values.
  WebGPU's shared read/write masks and reference must match all active faces;
  a culled face does not constrain the active face's values.
- Attachment clears draw only the requested clipped rectangles with replacement
  stencil state, optional depth writes and per-target colour masks. Restore the
  game's pipeline, viewport, scissor and reference for following draws. Report
  layered clears that the current 2D renderer cannot represent.

C++/C own guest capture; `web/vk-protocol.mjs` owns decoding;
`web/vkwebgpu.mjs` owns ordered replay and GPU state. Use existing plain structs
and command records, e.g. `p.u32(b.enable); p.u32(b.srcColor);` in the blend tail.
Do not add UI, change game assets or force brightness/lighting settings.

## Ordered work and checks

1. PS1: transport, native capture, guest exports and provenance. Add fail-first
   ABI/decoder/native emission tests; compile both freestanding guest shims.
2. PS2: stencil/depth GPU application and rectangle clears. Add renderer tests
   for replacement/equal stencil passes, dynamic state, culled-face handling,
   stencil load/store and restoring state after a partial clear.
3. PS3: build one coherent runtime, run Portal with normal lighting and input,
   inspect shadows and the opening/through-view, traverse it, then deploy and
   check public hashes. Update the compatibility report with actual limits.

Commands: `node --test web/tests/suite/unit/vulkan-stencil.test.mjs`, relevant V2
and Vulkan shim contract tests, `npm run typecheck`,
`python3 tools/rootfs64/build_vulkan_shims.py`, then the serialized
`tools/bw64-build.sh` JIT-capable build. Tests live under
`web/tests/suite/unit`; use the existing validation device fixture. Real game
checks use `web/tests/game-probe.mjs`, one emulator at a time.

The live build is `abde7983b`; rollback is its directory backup. Stage and deploy
all native/WASM/JS/dependency changes together. Do not push GitHub changes or
restart unrelated containers. Final acceptance remains open until PS3 passes.
