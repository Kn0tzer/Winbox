#!/usr/bin/env bash
# apply-runtime-poststeps.sh — the single automated path from freshly
# built/replaced rootfs zips to the complete, verified shipped runtime set.
#
# Why this exists: the web/runtime zips are finished by a series of
# idempotent post-steps (32-bit font payloads, DejaVu faces, the ALSA->OSS
# bridge, the winehq10 backports). They used to be applied by hand, one
# script at a time, from memory. On 2026-10-25 the census found the entire
# 32-bit ALSA->OSS bridge missing from the shipped zips because
# stage-alsa32.sh had never been run on them — while the sibling fonts32
# step had — and no check named the members. The completeness checker now
# names them, and this script makes the omission unreachable through the
# assemble-by-hand path:
#
#   * If the current set already verifies complete, it changes nothing
#     (the pinned zip bytes stay untouched) and says so.
#   * Otherwise it applies EVERY post-step, regenerates the artifact
#     manifest from the final zips, and refuses to succeed unless
#     check_runtime_completeness.py passes.
#
# After a run that changed artifact bytes, re-pin before committing:
# update tools/browser/runtime-assets.json for the changed zips and
# rebuild the retained runtime-assets archive (see runtime.md, re-pin
# addendum). bootstrap.py --check and the build.mjs gate both re-verify
# completeness, so a re-pin of a still-incomplete set cannot pass.
#
# The one post-step NOT run here is tools/browser/stage-wine32-fonts.py:
# it operates on the exported dist/runtime prefixes (font aliases +
# provenance), is already invoked by tools/browser/build.mjs on every
# export, and does not touch the canonical web/runtime set.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
cd "$REPO"

hashes() { sha256sum web/runtime/*.zip | sha256sum | cut -d' ' -f1; }
BEFORE="$(hashes)"

if python3 tools/rootfs64/check_runtime_completeness.py; then
    echo "apply-runtime-poststeps: shipped runtime set already complete — no changes."
    exit 0
fi

echo "apply-runtime-poststeps: runtime set incomplete; applying every post-step..."

# 32-bit glibc payloads: fontconfig/expat/DejaVuSans + fonts.conf
# (tools/rootfs32/stage-fonts.py; targets web/runtime and dist/runtime).
python3 tools/rootfs32/stage-fonts.py
# 32-bit DejaVu Bold/Mono faces (web/runtime and dist/runtime).
tools/rootfs64/stage-fonts32.sh
# 32-bit ALSA->OSS bridge: OSS plugins into wine32.zip, asound.conf +
# /usr/share/alsa into glibc-rootfs32.zip.
tools/rootfs64/stage-alsa32.sh web/runtime
if [ -d dist/runtime ]; then
    tools/rootfs64/stage-alsa32.sh dist/runtime
fi
# winehq10 backport DLLs into wine64.zip (web/runtime and dist/runtime);
# the merge-script path for when the container build is unavailable.
tools/rootfs64/stage-wine64-payloads.sh
# vkd3d D3D12 payload (bw64 vulkan-1.dll shim + vkd3d-proton d3d12 pair)
# into wine64.zip at system placement; same merge-script path.
tools/rootfs64/stage-vkd3d-payloads.sh
# vkd3d D3D12 payload, 32-bit (PE32 vkd3d-proton d3d12 pair from
# tools/vkd3d/build.w32) into wine32.zip at system placement; same
# merge-script path.
tools/rootfs64/stage-vkd3d-payloads32.sh

# The manifest is generated from the shipped zips; regenerate it from the
# final bytes before gating so the gate compares like with like.
python3 tools/rootfs64/gen_runtime_manifest.py

echo "apply-runtime-poststeps: gating on the completeness checker..."
python3 tools/rootfs64/check_runtime_completeness.py

AFTER="$(hashes)"
if [ "$BEFORE" != "$AFTER" ]; then
    echo "apply-runtime-poststeps: ARTIFACT BYTES CHANGED — re-pin" \
         "tools/browser/runtime-assets.json and rebuild the retained" \
         "runtime-assets archive before committing."
fi
echo "apply-runtime-poststeps: shipped runtime set complete and verified."
