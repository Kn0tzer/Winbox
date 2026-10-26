#!/bin/bash
# build-wine64-zip.sh — assemble the x86_64 Wine + glibc rootfs zips that
# Boxedwine64 mounts to run wine64 headless. Produces two layered zips that
# FsZip mounts at "/" (later layers override earlier):
#
#   glibc-rootfs64.zip   base: glibc dynamic linker + libc + wine64's lib deps
#   wine64.zip           overlay: the wine64 loader, wineserver, and the
#                        x86_64-unix / x86_64-windows Wine module trees
#
# At least one path in each trips FsZip::guestIs64 (x86_64-linux-gnu/, /lib64/,
# x86_64-unix/, x86_64-windows/) so the launcher treats the guest as 64-bit.
#
# The heavy apt-get is done ONCE into a committed image
# (boxedwine64/wine64-debian:bookworm); this script only stages + zips from it,
# so it is fast and repeatable. To rebuild the image from scratch:
#
#   docker run --name wine64build --platform linux/amd64 debian:bookworm sh -c \
#     'export DEBIAN_FRONTEND=noninteractive; apt-get update -qq && \
#      apt-get install -y -qq wine64'
#   docker commit wine64build boxedwine64/wine64-debian:bookworm
#   docker rm wine64build
#
# Output zips land in tools/rootfs64/dist/ (gitignored — 600MB+, build artifact).
set -euo pipefail

# --- host tool preflight ---------------------------------------------------
# Fail early, naming the cause: every host tool this script needs is
# checked before any work happens. Previously `zip` was first invoked at
# the very end (after all container + host staging) and `python3`
# mid-run, so a missing tool surfaced as a bare exit 127 with the
# expensive staging already done.
for tool in dirname mkdir rm cp docker python3 zip; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required host tool '$tool' not found in PATH — refusing to build." >&2
        exit 1
    fi
done

IMAGE="boxedwine64/wine64-debian:bookworm"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
DIST="$HERE/dist"
mkdir -p "$DIST"

# --- input preflight -------------------------------------------------------
# The container image and every prebuilt input are verified before the
# staging run, for the same reason: a missing input must fail HERE,
# naming itself, never degrade silently downstream. The libGL/libvulkan
# shims previously only warned when missing, so a "successful" run
# could ship rootfs zips with OpenGL or the Vulkan loader silently
# absent; they now hard-fail like the winehq10 backports and the ALSA
# tree at their staging points below. The two shims — and only they —
# have an explicit opt-in each, for workflows that deliberately source
# the shim elsewhere (the browser support pack also carries
# libvulkan.so.1; see tools/browser/stage-dependencies.py):
# BW64_ALLOW_NO_GL=1 / BW64_ALLOW_NO_VK=1 build without it and say so
# loudly, here, at the staging point, and in the closing summary.
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "ERROR: docker image $IMAGE not present — build it with the recipe in this script's header comment; refusing to build." >&2
    exit 1
fi
LIBGL_PREBUILT="$HERE/libgl64/libGL.so.1"
VK_PREBUILT="$HERE/libvk64/libvulkan.so.1"
WINEHQ64="$HERE/winehq10-x86_64"
ALSA_PREBUILT="$HERE/alsa"
OMIT_GL=0
OMIT_VK=0
if [ ! -f "$LIBGL_PREBUILT" ]; then
    if [ "${BW64_ALLOW_NO_GL:-}" = "1" ]; then
        OMIT_GL=1
        echo "=== WARNING: BW64_ALLOW_NO_GL=1 and $LIBGL_PREBUILT missing —" >&2
        echo "    building WITHOUT the guest libGL.so.1: OpenGL will be DISABLED in the guest. Deliberate omission. ===" >&2
    else
        echo "ERROR: $LIBGL_PREBUILT missing — run tools/rootfs64/build-libgl64.sh first" >&2
        echo "       (or set BW64_ALLOW_NO_GL=1 to build without guest OpenGL, loudly). Refusing to build." >&2
        exit 1
    fi
fi
if [ ! -f "$VK_PREBUILT" ]; then
    if [ "${BW64_ALLOW_NO_VK:-}" = "1" ]; then
        OMIT_VK=1
        echo "=== WARNING: BW64_ALLOW_NO_VK=1 and $VK_PREBUILT missing —" >&2
        echo "    building WITHOUT the guest libvulkan.so.1: Vulkan apps will find no Vulkan loader in the rootfs zips. Deliberate omission. ===" >&2
    else
        echo "ERROR: $VK_PREBUILT missing — run tools/rootfs64/buildvk.sh first" >&2
        echo "       (or set BW64_ALLOW_NO_VK=1 to build without the guest Vulkan loader, loudly). Refusing to build." >&2
        exit 1
    fi
fi
for dll in msmpeg2vdec.dll msvcp140_codecvt_ids.dll; do
    if [ ! -f "$WINEHQ64/$dll" ]; then
        echo "ERROR: $WINEHQ64/$dll missing — run the refresh recipe in $WINEHQ64/README.md; refusing to build." >&2
        exit 1
    fi
done
VKSHIM_PREBUILT="$REPO/tools/vkd3d/probe/bw64vulkan.dll"
D3D12_PREBUILT="$REPO/tools/vkd3d/build.w64/libs/d3d12/d3d12.dll"
D3D12CORE_PREBUILT="$REPO/tools/vkd3d/build.w64/libs/d3d12core/d3d12core.dll"
for f in "$VKSHIM_PREBUILT" "$D3D12_PREBUILT" "$D3D12CORE_PREBUILT"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: vkd3d payload $f missing - build vkd3d-proton (tools/vkd3d/BUILD.md) and the PE shim (tools/vkd3d/probe/VULKAN1_DLL_README.md) first; refusing to build a wine64.zip without the D3D12 payload." >&2
        exit 1
    fi
done
if [ ! -d "$ALSA_PREBUILT/usr/share/alsa" ]; then
    echo "ERROR: $ALSA_PREBUILT missing — refusing to build rootfs zips without the ALSA->OSS bridge." >&2
    exit 1
fi

echo "=== staging wine64 rootfs from $IMAGE (linux/amd64) ==="

STAGEHOST="$DIST/stage"
rm -rf "$STAGEHOST"; mkdir -p "$STAGEHOST"

# Run a container that stages a clean guest "/" tree into the mounted volume,
# computing the full runtime dependency closure of the wine64 loader,
# wineserver, and every x86_64-unix module via ldd, so the guest ld.so resolves
# every DT_NEEDED. We stage in the container (Linux paths/symlinks) but ZIP on
# the macOS host afterward (the image has no `zip`; it does have python3, but
# host zip keeps the artifact build off the slow emulated container).
docker run --rm --platform linux/amd64 -v "$DIST":/dist "$IMAGE" bash -c '
set -euo pipefail
STAGE=/dist/stage
mkdir -p "$STAGE"

# Collect the dependency closure: start from the wine64 ELF binaries + unix
# modules, ldd each, and copy every resolved host library (dedup via sort -u).
seeds=(
  /usr/lib/wine/wine64
  /usr/lib/wine/wineserver64
  /lib64/ld-linux-x86-64.so.2
)
for so in /usr/lib/x86_64-linux-gnu/wine/x86_64-unix/*.so; do seeds+=("$so"); done

# Resolve the transitive .so closure.
libs=$(
  for f in "${seeds[@]}"; do
    ldd "$f" 2>/dev/null | awk "/=>/ {print \$3} /ld-linux/ {print \$1}"
  done | grep -E "^/" | sort -u
)

copy() {  # copy a file preserving its absolute path under $STAGE, deref symlinks
  local src="$1"
  [ -e "$src" ] || return 0
  local real; real="$(readlink -f "$src")"
  local dstdir="$STAGE$(dirname "$src")"
  mkdir -p "$dstdir"
  cp -aL "$real" "$STAGE$src"
}

echo "--- copying glibc + lib closure ---"
for l in $libs; do copy "$l"; done
copy /lib64/ld-linux-x86-64.so.2
copy /lib/x86_64-linux-gnu/libc.so.6
# libgcc_s.so.1 is dlopened LAZILY by glibc for stack unwinding on pthread_exit,
# so ldd never lists it and it gets dropped — without it, every wine thread that
# exits aborts with "libgcc_s.so.1 must be installed for pthread_exit to work",
# which cascades into wineserver heap corruption during the registry save.
copy /lib/x86_64-linux-gnu/libgcc_s.so.1
copy /usr/lib/x86_64-linux-gnu/libgcc_s.so.1
# libfreetype: wine probes it for font rendering ("Wine cannot find the FreeType
# font library"); harmless to omit but cheap to ship.
copy /usr/lib/x86_64-linux-gnu/libfreetype.so.6
# the crypt32 unixlib dlopens libgnutls.so.30 at runtime (NOT via DT_NEEDED, so
# the ldd closure drops it). Without it the dlopen fails, its init
# leaves its entire .bss nt-callback table zero, and the first unix call
# jumps through a NULL slot (wild jump to 0x0 -> unimpl at 0x10270) — killing
# ANY process that loads crypt32, including BALDI.exe (a Unity title). Verified
# 2026-10-03 via BW64_DUMPADDR table dump + offline disassembly of the
# dlopen/dlsym init loop. Ship it AND its DT_NEEDED closure.
gnutls_dlopen_libs=(
  /usr/lib/x86_64-linux-gnu/libgnutls.so.30
  /usr/lib/x86_64-linux-gnu/libnettle.so.8
  /usr/lib/x86_64-linux-gnu/libhogweed.so.6
  /usr/lib/x86_64-linux-gnu/libgmp.so.10
  /usr/lib/x86_64-linux-gnu/libtasn1.so.6
  /usr/lib/x86_64-linux-gnu/libp11-kit.so.0
  /usr/lib/x86_64-linux-gnu/libffi.so.8
  /usr/lib/x86_64-linux-gnu/libidn2.so.0
  /usr/lib/x86_64-linux-gnu/libunistring.so.2
)
for gl in "${gnutls_dlopen_libs[@]}"; do copy "$gl"; done
# winex11.drv dlopens these X11 client libs at runtime (NOT via DT_NEEDED, so ldd
# never lists them and the closure above drops them — the base set libX11/libxcb/
# libXext IS pulled because the wine modules link it, but these higher libs are
# not). Without them, window/visual setup for the GL window aborts and the app
# exits cleanly (status=0) BEFORE the first gl64 trap — the glcube "stuck on the
# launcher screen" symptom seen on a real browser. Ship each one AND its own
# transitive closure (Xcursor needs Xfixes, Xrandr needs Xrender, etc.) so the
# guest ld.so resolves every DT_NEEDED. The guest searches /lib AND /usr/lib
# (x86_64-linux-gnu); copy() preserves the source path, so stage from /usr/lib
# (debian usr-merge: same inode as /lib) AND drop a /lib copy to match where the
# base X libs already live (the lib/ tree of glibc-rootfs64.zip).
x11_dlopen_libs=(
  /usr/lib/x86_64-linux-gnu/libXrender.so.1     # alpha/render visuals (core for GL win)
  /usr/lib/x86_64-linux-gnu/libXcursor.so.1      # themed cursors
  /usr/lib/x86_64-linux-gnu/libXfixes.so.3       # dep of Xcursor/Xcomposite
  /usr/lib/x86_64-linux-gnu/libXcomposite.so.1   # composite redirection
  /usr/lib/x86_64-linux-gnu/libXi.so.6           # XInput2
  /usr/lib/x86_64-linux-gnu/libXinerama.so.1     # multi-monitor probe
  /usr/lib/x86_64-linux-gnu/libXxf86vm.so.1      # mode/gamma probe
  /usr/lib/x86_64-linux-gnu/libXrandr.so.2       # randr (commonly probed alongside)
)
mkdir -p "$STAGE/lib/x86_64-linux-gnu"
for xl in "${x11_dlopen_libs[@]}"; do
  copy "$xl"                                       # -> /usr/lib/x86_64-linux-gnu
  [ -e "$xl" ] && cp -aL "$(readlink -f "$xl")" "$STAGE/lib/x86_64-linux-gnu/$(basename "$xl")"
  # also pull the resolved closure of each lib (deref real path; ldd lists deps)
  if [ -e "$xl" ]; then
    for dep in $(ldd "$(readlink -f "$xl")" 2>/dev/null | awk "/=>/ {print \$3}" | grep -E "^/" | sort -u); do
      copy "$dep"
      cp -aL "$(readlink -f "$dep")" "$STAGE/lib/x86_64-linux-gnu/$(basename "$dep")" 2>/dev/null || true
    done
  fi
done
mkdir -p "$STAGE/etc"; : > "$STAGE/etc/ld.so.cache"

echo "--- copying wine64 loader + wineserver + module trees ---"
copy /usr/lib/wine/wine64
copy /usr/lib/wine/wineserver64
copy /usr/lib/wine/wineapploader
# Do NOT copy /usr/lib/wine/wineserver: on Debian that is a /bin/sh dispatch
# script (selects wineserver32/64 at runtime), and the guest rootfs64 has no
# /bin/sh, so the shipped script cannot execute. Ship the name as a symlink
# to the ELF wineserver64 instead, mirroring usr/bin/wineserver below and
# the bridge of the 32-bit pipeline (tools/build-prefix32.sh). The launcher also
# sets WINESERVER=/usr/lib/wine/wineserver64 for every spawn, so this path
# is latent insurance for any guest exec of the compiled-in server path.
ln -sf /usr/lib/wine/wineserver64 "$STAGE/usr/lib/wine/wineserver"
mkdir -p "$STAGE/usr/lib/x86_64-linux-gnu/wine"
cp -aL /usr/lib/x86_64-linux-gnu/wine/x86_64-unix    "$STAGE/usr/lib/x86_64-linux-gnu/wine/"
cp -aL /usr/lib/x86_64-linux-gnu/wine/x86_64-windows "$STAGE/usr/lib/x86_64-linux-gnu/wine/"
# i386-windows is the 32-bit PE side; not needed for a pure wine64 boot.

# Wine data: NLS locale tables (wineserver loads l_intl.nls), fonts, etc.
# wine resolves these as <bindir>/../../share/wine, i.e. /usr/share/wine.
mkdir -p "$STAGE/usr/share"
cp -aL /usr/share/wine "$STAGE/usr/share/"
# X11 locale DATA (libx11-data: /usr/share/X11/locale/{locale.alias,locale.dir,
# compose.dir,<locale>/XLC_LOCALE,...}). libX11 reads these during XOpenIM /
# XSetLocaleModifiers when a GUI app (e.g. glcube via winex11) opens the display;
# missing them, X locale init fails and the app exits cleanly (status=0) BEFORE
# any gl64 GL trap — the second layer of the "stuck on launcher" symptom after the
# dlopen X libs above. Pure data (no DT_NEEDED), so the ldd closure never pulls it.
if [ -d /usr/share/X11/locale ]; then
  mkdir -p "$STAGE/usr/share/X11"
  cp -aL /usr/share/X11/locale "$STAGE/usr/share/X11/"
fi

# Convenience launch symlinks at canonical wine paths.
mkdir -p "$STAGE/usr/bin"
ln -sf /usr/lib/wine/wine64      "$STAGE/usr/bin/wine64"
ln -sf /usr/lib/wine/wineserver64 "$STAGE/usr/bin/wineserver"

# A minimal HOME so wine has somewhere to create its prefix.
mkdir -p "$STAGE/root"

# Standard writable scratch dirs a Linux rootfs must have. wineserver creates
# its socket tmpdir under /tmp (falling back from /run/user/<uid>), and wine
# chdir/stat the prefix HOME. Without these the guest mkdir(/tmp/wine-XXX)
# fails ENOENT (no parent) and wineserver setup aborts. Ship them in the zip so
# the parent node always exists; the kernel overlays the writable native root
# on top for the actual file creation.
# HOME defaults to /home/username (the writable-prefix path that the kernel
# getMode() allows); it must exist so wine can create $HOME/.wine and the
# dosdevices/c: symlink (under /winePrefix those symlinks fail EACCES).
mkdir -p "$STAGE/tmp" "$STAGE/run/user/1000" "$STAGE/home/username" "$STAGE/var/tmp"
# /etc/localtime: wine/glibc reads this as a TZif file on every time op AND
# RtlQueryTimeZoneInformation re-reads it from its idle loop. It must be a
# VALID TZif2 file — a v1 block followed by a v2 block ending in a "\nUTC0\n"
# POSIX footer — or glibc __tzfile_read keeps rejecting it and wine never
# settles. A bare `ln -sf /usr/share/zoneinfo/UTC` produces a DANGLING symlink
# (zoneinfo is not shipped in the guest) and the UTC file of the container is a
# truncated 57-byte stub, so embed a known-good 114-byte UTC TZif inline.
printf %s \
  "VFppZjIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAQAAAAAAABVVEMAVFppZjIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAQAAAAAAABVVEMAClVUQzAK" \
  | base64 -d > "$STAGE/etc/localtime"

echo "--- staged tree ready ---"
du -sh "$STAGE"
'

# Stage our custom guest libGL.so.1 (the 64-bit OpenGL shim wine dlopens; it
# traps gl*/glX* to the Boxedwine64 host — see source/opengl/gl64bridge*). It's a
# freestanding prebuilt artifact (tools/rootfs64/build-libgl64.sh) copied in on
# the host AFTER the container stages glibc, so wine's loader finds libGL.so.1 on
# the standard path and 3D apps don't hit "GLX is missing, disabling OpenGL".
if [ -f "$LIBGL_PREBUILT" ]; then
    echo "--- staging custom guest libGL.so.1 ---"
    mkdir -p "$STAGEHOST/lib/x86_64-linux-gnu" "$STAGEHOST/usr/lib/x86_64-linux-gnu"
    cp "$LIBGL_PREBUILT" "$STAGEHOST/lib/x86_64-linux-gnu/libGL.so.1"
    # also drop a copy on the usr path some wine builds search first
    cp "$LIBGL_PREBUILT" "$STAGEHOST/usr/lib/x86_64-linux-gnu/libGL.so.1"
else
    # Unreachable via the input preflight unless the file vanished
    # mid-run; keep the same contract here: never degrade silently.
    if [ "$OMIT_GL" = "1" ]; then
        echo "WARNING: guest libGL.so.1 omitted (BW64_ALLOW_NO_GL=1) — OpenGL disabled in the guest." >&2
    else
        echo "ERROR: $LIBGL_PREBUILT missing — refusing to build rootfs zips without guest OpenGL." >&2
        exit 1
    fi
fi

# Backport two builtin-Wine DLLs that exist in the 32-bit WineHQ-10.0 tree
# but not in Debian's 8.0 amd64 tree: msmpeg2vdec.dll (Media Foundation
# H.264/MPEG-2 decoder - the decoder behind virtually every WMV/H.264 game
# cutscene) and msvcp140_codecvt_ids.dll. Both import only against module
# sets that wine64 8.0 already ships (msmpeg2vdec needs mfplat's
# MFTRegister/MFTUnregister, verified present). See winehq10-x86_64/README.md
# for the pinned source and hashes. (d3d12core.dll used to be the third
# backport; it is now staged from the vkd3d-proton build by the payload
# block below - Wine's own d3d12core has no DXIL frontend and cannot
# serve D3D12 apps.)
for dll in msmpeg2vdec.dll msvcp140_codecvt_ids.dll; do
    if [ -f "$WINEHQ64/$dll" ]; then
        cp "$WINEHQ64/$dll" \
            "$STAGEHOST/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/$dll"
        echo "--- staging $dll (winehq10 backport) ---"
    else
        # Hard failure, not a warning: check_runtime_completeness.py
        # requires these members in the shipped wine64.zip, so a zip built
        # without them is unshippable. Fail here, naming the cause,
        # instead of producing a silently incomplete artifact.
        echo "ERROR: $WINEHQ64/$dll missing — run the refresh recipe in $WINEHQ64/README.md;" \
             "refusing to build a wine64.zip that lacks the module." >&2
        exit 1
    fi
done

# Stage the D3D12 payload at system placement in the wine tree (the
# prefix's system32 is a .link into this tree, so this is where every
# guest resolves these names):
#   bw64vulkan.dll <- the bw64 PE shim (tools/vkd3d/probe/bw64vulkan.dll),
#     under its OWN name. The vendored vkd3d-proton's loader tries
#     "bw64vulkan.dll" FIRST (Boxedwine64 patch, tools/vkd3d/src/libs/
#     d3d12core/main.c) precisely to bypass Wine's winevulkan extension
#     filtering: Wine 8.0's winevulkan conversion tables predate
#     VK_KHR_maintenance5/6 (its Vulkan 1.3.239 registry knows only
#     maintenance1/2), and vkd3d-proton requires maintenance5/6 at
#     device creation. The shim forwards PE Vulkan calls straight to
#     the host vk64bridge with pointers uninterpreted, so vkd3d sees
#     the bridge's real caps. Wine's vulkan-1.dll forwarder and the
#     winevulkan path the rest of the fleet uses stay untouched, as do
#     ELF clients (they use libvulkan.so.1 below).
#   d3d12.dll + d3d12core.dll <- the vkd3d-proton build, replacing
#     Wine's pair so D3D12 apps are served at system placement with
#     WINEDLLOVERRIDES=d3d12=n;d3d12core=n, no app-dir copies.
# The merge-script equivalent for this ARM64 host (where the container
# staging crashes) is tools/rootfs64/stage-vkd3d-payloads.sh.
echo "--- staging vkd3d payload (bw64vulkan.dll shim + d3d12 pair) ---"
cp "$VKSHIM_PREBUILT" "$STAGEHOST/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/bw64vulkan.dll"
cp "$D3D12_PREBUILT" "$STAGEHOST/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d12.dll"
cp "$D3D12CORE_PREBUILT" "$STAGEHOST/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d12core.dll"

# Stage our guest Vulkan shim (tools/rootfs64/libvk64/libvk64.c -> libvulkan.so.1).
# Same shape as the libGL.so.1 staging above, for the same reason: with no Vulkan
# loader and no ICD .so in the guest, THIS library is the guest's whole Vulkan API
# surface — it exports the 85 vk* entry points that vkcube/DXVK resolve through
# vkGetInstanceProcAddr, and each one traps to the Boxedwine64 host on the private
# syscall 0x564B0000 (source/vulkan/vk64bridge*). It is also the answer to
# Loader requirement: the committed rootfs shipped NO libvulkan.so.1, so
# the guest's loader probing /lib/x86_64-linux-gnu and /usr/lib/x86_64-linux-gnu
# (and /lib, /usr/lib) all failed with ENOENT. Build it with
# tools/rootfs64/buildvk.sh.
VK_FIXTURE="$HERE/libvk64/vkfixture"
if [ -f "$VK_PREBUILT" ]; then
    python3 "$HERE/build_vulkan_shims.py" --check --arch 64
    echo "--- staging custom guest libvulkan.so.1 ---"
    mkdir -p "$STAGEHOST/lib/x86_64-linux-gnu" "$STAGEHOST/usr/lib/x86_64-linux-gnu"
    cp "$VK_PREBUILT" "$STAGEHOST/lib/x86_64-linux-gnu/libvulkan.so.1"
    # also drop a copy on the usr path some loaders search first
    cp "$VK_PREBUILT" "$STAGEHOST/usr/lib/x86_64-linux-gnu/libvulkan.so.1"
    if [ -f "$VK_FIXTURE" ]; then
        # the P2 boundary fixture: a guest ELF that walks vkcube's whole Vulkan
        # path through the shim (see tools/rootfs64/libvk64/vkfixture.c). Run it
        # with web/tests/scratch-vk.mjs.
        echo "--- staging vkfixture (P2 boundary fixture) ---"
        mkdir -p "$STAGEHOST/usr/bin"
        cp "$VK_FIXTURE" "$STAGEHOST/usr/bin/vkfixture"
    fi
else
    # Same contract as the libGL branch above: never degrade silently.
    if [ "$OMIT_VK" = "1" ]; then
        echo "WARNING: guest libvulkan.so.1 omitted (BW64_ALLOW_NO_VK=1) — no Vulkan loader in the rootfs zips." >&2
    else
        echo "ERROR: $VK_PREBUILT missing — refusing to build rootfs zips without the guest Vulkan loader." >&2
        exit 1
    fi
fi

# Stage the ALSA->OSS sound bridge (M5). wine's winealsa needs libasound to
# resolve "default"; the rootfs has no real ALSA cards, so we route it to the
# emulated OSS /dev/dsp (KDspAudio -> SDL/WebAudio):
#   - /usr/share/alsa            libasound's base config (without it snd_pcm_open
#                                fails for ANY name — this was why audio never worked)
#   - alsa-lib/*_oss.so          the pcm_oss/ctl_oss plugins (Debian libasound2-plugins)
#   - /etc/asound.conf           default = plug->oss:/dev/dsp (plug converts
#                                rate/format so any app format reaches the sink)
# The artifacts are checked in under tools/rootfs64/alsa (436K); refresh from
# Debian with the docker one-liner in tools/rootfs64/alsa/README if needed.
if [ -d "$ALSA_PREBUILT/usr/share/alsa" ]; then
    echo "--- staging ALSA->OSS sound bridge ---"
    mkdir -p "$STAGEHOST/usr/lib/x86_64-linux-gnu/alsa-lib" "$STAGEHOST/usr/share" "$STAGEHOST/etc"
    cp "$ALSA_PREBUILT"/usr/lib/x86_64-linux-gnu/alsa-lib/*.so "$STAGEHOST/usr/lib/x86_64-linux-gnu/alsa-lib/"
    cp -R "$ALSA_PREBUILT/usr/share/alsa" "$STAGEHOST/usr/share/"
    printf 'pcm.!default {\n    type plug\n    slave.pcm {\n        type oss\n        device /dev/dsp\n    }\n}\nctl.!default {\n    type oss\n    device /dev/mixer\n}\n' > "$STAGEHOST/etc/asound.conf"
else
    # Hard failure, not a warning: without this tree the zips also lose
    # /etc/asound.conf (generated in the block above), and
    # check_runtime_completeness.py requires the full bridge member set
    # in the shipped artifacts. A silently bridgeless build is exactly
    # the 2026-10-25 32-bit defect, one arch over.
    echo "ERROR: $ALSA_PREBUILT missing — refusing to build rootfs zips without the ALSA->OSS bridge." >&2
    exit 1
fi

# GStreamer demux/decode plugins + DT_NEEDED closure are NOT staged into
# the boot zips: since the 2026-10-25 lazy split they ride the support
# pack (tools/browser/stage-dependencies.py stages the gstreamer64 tree
# produced by prepare-gstreamer.py into the lazy archive, mounted at the
# same guest paths before Wine starts). winegstreamer.so links only the
# gst core, which ships with the base tree above; the plugin closure must
# stay out of this zip — check_runtime_completeness.py pins its absence.

echo "=== zipping on host (macOS zip preserves the symlinks) ==="
rm -f "$DIST/glibc-rootfs64.zip" "$DIST/wine64.zip"
# Base: glibc + deps. Overlay: wine trees + launchers. -y keeps symlinks as links.
( cd "$STAGEHOST" && zip -qry9 "$DIST/glibc-rootfs64.zip" lib64 lib etc tmp run home var )
( cd "$STAGEHOST" && zip -qry9 "$DIST/wine64.zip" usr root )
echo "=== zip sizes ==="
ls -lh "$DIST"/*.zip || echo "(could not list zip sizes)" >&2
if [ "$OMIT_GL" = "1" ] || [ "$OMIT_VK" = "1" ]; then
    omitted=""
    [ "$OMIT_GL" = "1" ] && omitted="$omitted guest libGL.so.1"
    [ "$OMIT_VK" = "1" ] && omitted="$omitted guest libvulkan.so.1"
    echo "=== NOTE: this build deliberately omits:$omitted (explicit opt-in) ===" >&2
fi
echo "=== done. zips in $DIST ==="
