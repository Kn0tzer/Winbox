# tools/rootfs64 — guest runtime staging

Everything that ships inside the guest runtime is assembled here: the
Wine + glibc archives the emulator mounts at `/`, the initialized Wine
prefix overlay, the guest-side OpenGL/Vulkan shims, and the fonts,
audio and support payloads — plus the manifest and completeness checks
the browser export is verified against. Build coordination for the
generated artifacts is in the repo-root `BUILD_PROTOCOL.md`; the
claims about the shipped set live in `docs/known-gaps/runtime.md`.

## Assemble and verify the shipped set

- `apply-runtime-poststeps.sh` — the single automated path from
  freshly built or replaced rootfs zips to the complete, verified
  shipped runtime set.
- `check_runtime_completeness.py` — the completeness guard behind the
  runtime register: verifies that the shipped artifacts match what
  `docs/known-gaps/runtime.md` claims about them.
- `gen_runtime_manifest.py` — generates
  `docs/known-gaps/runtime-artifact-manifest.md` from the shipped
  artifacts. That register file is machine-made; never hand-edit it.

## Wine packages and prefix

- `build-wine64-zip.sh` — assembles the x86_64 Wine + glibc rootfs
  zips (layered; later layers override earlier ones).
- `build-prefix64.sh` — (re)assembles the initialized Wine prefix
  overlay the launcher mounts as a third zip; `prefix64-reg.txt` is
  part of that state. `prepare_prefixes.py` prepares prefix content
  and `test_prepare_prefixes.py` checks it; `set-prefix-audio.py`
  ensures a prefix selects the ALSA audio driver; `fontconfig/`
  carries the guest font configuration and its prefix patcher.

The staged Wine is two vintages mixed deliberately — the x86_64 set
is Debian bookworm Wine 8.0, with individual DLLs backported from
WineHQ 10.0 and four core DLLs rebuilt from the pinned Wine 8.0
source with loader-surface backports. Why, and which DLLs:
`docs/reports/loader-gaps-20261030.md`.

- `wine8-backports-x86_64/` — the rebuilt Wine 8.0 backport DLLs;
  `build-wine8-backports.sh` rebuilds them from the pinned source,
  `stage-wine8-backports.sh` stages them.
- `winehq10-x86_64/` — the WineHQ 10.0 DLLs backported into the
  Debian tree; `stage-wine64-payloads.sh` merges them into the
  shipped archives without a full rootfs rebuild.

## Guest shims and graphics payloads

- `libgl32/`, `libgl64/` — the guest OpenGL shims (`libGL.so.1`) that
  Wine loads; `build-libgl32.sh` / `build-libgl64.sh` compile and
  stage them.
- `libvk32/`, `libvk64/` — the guest Vulkan shims;
  `build_vulkan_shims.py` builds or verifies both, `buildvk.sh`
  builds and stages them into the rootfs zips, and
  `patch-runtime-shim.py` swaps the shims inside an already-built
  runtime export.
- `stage-vkd3d-payloads.sh`, `stage-vkd3d-payloads32.sh` — merge the
  vkd3d-proton D3D12 payload and the bw64 Vulkan PE shim into the
  shipped archives without a full rootfs rebuild.
- `gltest/` — guest test programs, including the `deskpin` helpers
  the browser export substitutes into the pinned prefix archives
  (provenance for that swap is covered in `BUILD_PROTOCOL.md`).

## Support payloads

- `alsa/`, `alsa32/`, `stage-alsa32.sh` — the ALSA-to-OSS bridge;
  the script adds the staged i386 bridge to the 32-bit archives.
- `fonts32/`, `stage-fonts32.sh` — the DejaVu Bold/Mono faces the
  32-bit rootfs otherwise lacks.
- `prepare-gstreamer.py` — stages the GStreamer plugins Wine's
  winegstreamer needs (shipped in the lazy support pack, not the
  boot zips).
- `prepare-mono32.py` — restores the pinned Wine Mono payload used
  by the shared browser runtime.
- `nss-overlay/` — a name-service overlay tree for the guest.
- `apps/` — bundled guest applications. `games/` — guest game
  utilities and their build script.

## Release helpers

- `split-rootfs.sh` — splits the large 64-bit rootfs zips into
  <100 MB chunks. `publish-rootfs-release.sh` uploads the split
  rootfs to a GitHub Release.

## Standalone-kernel fixtures: `work/` and `root/`

This directory began as the 64-bit glibc rootfs for the standalone
kernel runner, and that fixture tree is still here: `work/` holds
probe sources (a glibc hello-world, directory and threading probes,
the usefoo/libfoo versioned-DSO test) and `root/` is the mountable
fixture rootfs built from them (busybox, `hello_glibc`,
`hello_static`, and the probes under `bin/`). They run through
`tools/run_x64_root.sh`, e.g.
`tools/run_x64_root.sh /bin/busybox ls -la /`.

Earlier versions of this file narrated that milestone's blockers and
next steps. They described a pre-Wine system that no longer exists —
dynamic glibc under Wine is the product now — so the narrative was
removed; it remains in this file's git history.
