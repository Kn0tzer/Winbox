# Runtime regression checkpoint — 2026-10-13

The full browser suite passed all 11 files. The fast suite passed all 104
unit files and the legacy checks (73 passed, one existing skip). Native
checks passed 602 CPU32 cases, 315 CPU64 cases and the six Vulkan bridge
harness groups. Type checking and the browser runtime build passed.

The tested runtime WASM SHA-256 is `d8ad843c953fed7b5dced8322bf535911e4d44e0e3252b734dda7db4d6c20c3f`. Local evidence
is in `test-results/suite-*/`; D3D12 includes `triangle.png` and `pixels.json`.
Its clear and draw/present programs returned zero, and its compositor
screenshot contained 6,090 red-dominant triangle pixels. Input checks
observed all six key-down/key-up pairs and the relative mouse packet,
burst and negative-delta cases in both guest CPU modes. Audio checked actual
64-bit DSP output PCM, including nonzero samples and RMS.

The fixes cover UNIX acceptance/cancellation, fork descriptor snapshots,
audio callbacks outside the registry lock, guest terminal line framing,
32-bit standalone readiness, Vulkan PE proc lookup and WithCount argument
adapters, and pipeline binding before render attachments exist. Native
regressions reproduce the corresponding failures. The first D3D9 allocator
failure and its symbolized fork stack are preserved in the functional
backup directory; subsequent focused and full browser runs passed.

Celeste 1.4.0.0 reached `GAME DISPLAYED` after loading, but its captured canvas
was black. This boot result does not establish rendering or playability.
Interactive Celeste, Portal and Gang Beasts verification and the requested
optimization phase remain outstanding. These tests exercise selected
operations; they do not establish complete coverage of every API or variant.
