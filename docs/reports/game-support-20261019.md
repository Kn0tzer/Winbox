# Portal and Gang Beasts support — 2026-10-19

Portal takes priority, followed by Gang Beasts. Optimization follows functional
acceptance of both games. The CPU JIT settings rollout is complete and is not
being revalidated here.

## Portal rendering

The current combined interpreter/JIT runtime (`7b4ab076519b`) reaches Portal's
main menu and starts a level. The CPU32 SSE3 repair has fixed the earlier DXVK
allocation overlap: a read-only scan found 95 nonzero offsets among 101 bound
images, rather than almost every texture sharing offset zero. Evidence:
`test-results/portal-resume-20261019/image-bindings.json`.

The live level still had corrupted textures. The captured frame can reproduce
that defect independently of the guest's startup:
`test-results/portal-resume-20261019/scene-frame.json.gz`.
Its source textures decode correctly with an independent DDS decoder.

The SPIR-V pre-pass splits Vulkan combined image/sampler descriptors into separate
WebGPU bindings. The renderer looked up sampling state using the new sampler
binding, which does not exist in the guest descriptor set. Consequently it used
default nearest/clamped sampling throughout the scene. The generated sampler
identifier now preserves the original descriptor binding, including when the
input SPIR-V has no names. Resource scanning carries that binding to descriptor
lookup and bind-group cache fingerprints; the WebGPU binding stays unchanged.

Replaying the same frame restores wall/floor texture detail. The repaired replay
has 163 sampler cache hits, versus zero before the repair, with no rejected
frames. Compare `test-results/portal-scene-replay-20261019/frame.png` and
`test-results/portal-sampler-replay-20261019/frame.png`. This is rendering evidence,
not completion of live gameplay acceptance.

Some live draws also rejected valid odd-length uint16 index streams because
WebGPU mapped buffer sizes must be divisible by four. Allocation now rounds up
while preserving every source byte. The fail-first renderer regression exercises
three uint16 indices and checks both the resulting allocation and contents.

Focused launch, shader translation, buffer replay and OCR checks pass (18 tests);
type checking passes. OCR uses one OpenMP thread so recognizing the menu does
not time out under emulator load. Browser probes match versioned launcher URLs,
and can exercise the ordinary launcher without graphics overlays.

## Remaining acceptance

A normal-launcher Portal run is checking the shared Direct3D installation and
live level rendering/input. Gang Beasts follows Portal; neither game is yet
claimed fully playable by this report.
