#!/bin/bash
# Merge the vkd3d-proton D3D12 payload + the bw64 Vulkan PE shim into the
# shipped wine64.zip archives WITHOUT a full rootfs rebuild.
#
# What goes in, and why (DirectX 12 campaign, 2026-10-26 restage):
#   usr/lib/x86_64-linux-gnu/wine/x86_64-windows/bw64vulkan.dll
#       <- tools/vkd3d/probe/bw64vulkan.dll (the bw64 PE shim: forwards
#          Vulkan calls from PE clients straight to the host vk64bridge
#          via the private syscall, passing pointers uninterpreted).
#          The vendored vkd3d-proton's loader tries "bw64vulkan.dll"
#          FIRST (Boxedwine64 patch, tools/vkd3d/src/libs/d3d12core/
#          main.c: it bypasses Wine's winevulkan extension filtering,
#          whose Debian-8.0 tables — Vulkan 1.3.239 registry — know only
#          VK_KHR_maintenance1/2, while vkd3d requires maintenance5/6 at
#          device creation). Probes stage the shim next to the exe; this
#          puts it at system placement (the prefix's system32 is a .link
#          into this tree), so ANY D3D12 program in the demo environment
#          finds it by plain LoadLibrary search — no app-dir copies, no
#          DLL overrides.
#   usr/lib/x86_64-linux-gnu/wine/x86_64-windows/vulkan-1.dll
#   usr/lib/x86_64-linux-gnu/wine/x86_64-windows/winevulkan.dll
#       <- the SAME shim bytes, under both other names a PE client can
#          load it by. System-placement surface closure (2026-10-27,
#          surface lane; campaign report 4.19): Wine's builtin dxgi.dll
#          creates D3D12 swapchain surfaces through a factory that
#          LoadLibrary's "winevulkan.dll" BY NAME (proven by a
#          WINEDEBUG=+module loader trace of the failing probe:
#          load_dll "winevulkan.dll"; vulkan-1.dll is never loaded in
#          the D3D12 flow), while native Vulkan apps and DXVK's dxgi
#          load "vulkan-1.dll". winevulkan treats a VkInstance as a
#          pointer to its own wrapper struct, but vkd3d's instance
#          (created through bw64vulkan.dll above) is a bridge handle
#          (an opaque id, not a pointer), so dxgi's
#          vkCreateWin32SurfaceKHR faulted inside winevulkan
#          (loader_thunks.c:3097 assert) and every system-placement
#          D3D12 swapchain aborted before any surface existed.
#          Resolving all three PE Vulkan names to the shim puts every
#          PE Vulkan client in one handle domain (the bridge) — the
#          configuration the unstaged first-light probe already proved
#          (it stages the shim under these names in the app dir). No
#          shipped Wine PE statically imports either name (import
#          scan, 2026-10-27); consumption is dynamic only.
#   usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d12.dll
#   usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d12core.dll
#       <- the vkd3d-proton 3.1.0 build (tools/vkd3d/build.w64, see
#          tools/vkd3d/BUILD.md), replacing Wine's d3d12 pair at system
#          placement so D3D12 apps resolve them from the wine tree with
#          WINEDLLOVERRIDES=d3d12=n;d3d12core=n. d3d12core.dll was
#          previously the winehq10 backport; the pair must move
#          together (proton's d3d12.dll is a thin loader for its own
#          d3d12core.dll).
#
# The canonical path is build-wine64-zip.sh (container staging), which
# performs exactly these copies; this script is the merge-script path
# for use when the container build is unavailable — on this ARM64 host
# the linux/amd64 container's binfmt/QEMU layer crashes during staging
# (x86_64-binfmt-P: QEMU internal SIGSEGV, 2026-10-25). Idempotent:
# archives whose members already match the payload bytes are skipped.
# Mirrors tools/rootfs64/stage-wine64-payloads.sh.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WIN64_DIR="usr/lib/x86_64-linux-gnu/wine/x86_64-windows"
VKSHIM="$REPO/tools/vkd3d/probe/bw64vulkan.dll"
D3D12="$REPO/tools/vkd3d/build.w64/libs/d3d12/d3d12.dll"
D3D12CORE="$REPO/tools/vkd3d/build.w64/libs/d3d12core/d3d12core.dll"
for f in "$VKSHIM" "$D3D12" "$D3D12CORE"; do
    [[ -f "$f" ]] || { echo "stage-vkd3d-payloads: missing payload $f (see tools/vkd3d/BUILD.md and tools/vkd3d/probe/VULKAN1_DLL_README.md)" >&2; exit 2; }
done

payload_matches() {
    python3 - "$1" "$VKSHIM" "$D3D12" "$D3D12CORE" <<'EOF'
import sys, zipfile
archive, vkshim, d3d12, d3d12core = sys.argv[1:5]
want = {
    "usr/lib/x86_64-linux-gnu/wine/x86_64-windows/bw64vulkan.dll": open(vkshim, "rb").read(),
    "usr/lib/x86_64-linux-gnu/wine/x86_64-windows/vulkan-1.dll": open(vkshim, "rb").read(),
    "usr/lib/x86_64-linux-gnu/wine/x86_64-windows/winevulkan.dll": open(vkshim, "rb").read(),
    "usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d12.dll": open(d3d12, "rb").read(),
    "usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d12core.dll": open(d3d12core, "rb").read(),
}
with zipfile.ZipFile(archive) as z:
    names = set(z.namelist())
    ok = all(n in names and z.read(n) == data for n, data in want.items())
sys.exit(0 if ok else 1)
EOF
}

stamp="$(date -u +%Y%m%d-%H%M%S)"
for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-vkd3d-payloads: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    archive="$runtime/wine64.zip"
    [[ -f "$archive" ]] || { echo "stage-vkd3d-payloads: expected $archive" >&2; exit 2; }
    if payload_matches "$archive"; then
        echo "stage-vkd3d-payloads: $archive already carries the vkd3d payloads — skipping"
        continue
    fi
    backup="/home/ubuntu/backups/winbox-vkd3d-payloads-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$(dirname "$runtime")")-$(basename "$runtime")-wine64.zip"
    TMP="$(mktemp -d)"
    mkdir -p "$TMP/payload/$WIN64_DIR"
    cp "$VKSHIM" "$TMP/payload/$WIN64_DIR/bw64vulkan.dll"
    cp "$VKSHIM" "$TMP/payload/$WIN64_DIR/vulkan-1.dll"
    cp "$VKSHIM" "$TMP/payload/$WIN64_DIR/winevulkan.dll"
    cp "$D3D12" "$TMP/payload/$WIN64_DIR/d3d12.dll"
    cp "$D3D12CORE" "$TMP/payload/$WIN64_DIR/d3d12core.dll"
    ( cd "$TMP/payload" && zip -qry9 "$TMP/payload.zip" usr )
    python3 - "$archive" "$TMP/payload.zip" <<'EOF'
import os, sys, zipfile
dst, src = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst) as zdst, zipfile.ZipFile(src) as zsrc:
    fresh = {info.filename: zsrc.read(info.filename) for info in zsrc.infolist()}
    with zipfile.ZipFile(dst + '.merged', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for info in zdst.infolist():
            if info.filename not in fresh:
                out.writestr(info, zdst.read(info.filename))  # keep ZipInfo: symlink bits (external_attr), dates, modes
        for name, data in fresh.items():
            out.writestr(name, data)
os.replace(dst + '.merged', dst)
EOF
    rm -rf "$TMP"
    payload_matches "$archive" \
        || { echo "stage-vkd3d-payloads: final verification failed for $archive" >&2; exit 2; }
    echo "stage-vkd3d-payloads: vkd3d payloads staged in $archive (backup in $backup)"
done
