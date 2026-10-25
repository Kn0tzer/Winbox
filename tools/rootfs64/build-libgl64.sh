#!/bin/bash
# build-libgl64.sh — compile the guest libGL.so.1 (the 64-bit OpenGL shim wine
# dlopens) and stage the artifact in tools/rootfs64/libgl64/libGL.so.1.
#
# The shim is plain freestanding C (libgl64.c) that traps every gl*/glX* call to
# the Boxedwine64 host via a private syscall (see source/opengl/gl64bridge*). It
# must be a real x86_64-linux ELF .so with SONAME=libGL.so.1 exporting
# glXGetProcAddressARB + the GLX/core-GL entry points winex11/opengl32 resolve.
#
# We compile inside the same linux/amd64 Debian image used for the rootfs so the
# ABI matches exactly. gcc is installed into a one-shot container (cheap; the
# result is committed to the repo so the heavy rootfs build just copies it).
#
# Usage:  tools/rootfs64/build-libgl64.sh [--check]
# Output: tools/rootfs64/libgl64/libGL.so.1   (commit this)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/libgl64"
IMAGE="boxedwine64/wine64-debian:bookworm"

[ -f "$SRC/libgl64.c" ] || { echo "error: $SRC/libgl64.c missing" >&2; exit 1; }

# --check: rebuild in a scratch copy and byte-compare against the committed
# binary, without modifying the worktree. Modelled on
# build_vulkan_shims.py --check; stage-dependencies.py runs this before
# staging, so a stale committed binary cannot ship silently (the libgl32
# no-op-stub divergence was only caught by a unit test, after the fact).
if [ "${1:-}" = "--check" ]; then
    SCRATCH="$(mktemp -d)"
    trap 'rm -rf "$SCRATCH"' EXIT
    mkdir -p "$SCRATCH/rootfs64"
    cp "$HERE/build-libgl64.sh" "$SCRATCH/rootfs64/build-libgl64.sh"
    cp -a "$SRC" "$SCRATCH/rootfs64/libgl64"
    bash "$SCRATCH/rootfs64/build-libgl64.sh" >/dev/null
    if cmp -s "$SCRATCH/rootfs64/libgl64/libGL.so.1" "$SRC/libGL.so.1"; then
        echo "libgl64: committed libGL.so.1 matches a fresh build of libgl64.c"
        exit 0
    fi
    echo "error: committed tools/rootfs64/libgl64/libGL.so.1 does not match a fresh build of libgl64.c; rerun tools/rootfs64/build-libgl64.sh and commit the binary" >&2
    exit 1
fi

echo "=== building guest libGL.so.1 (linux/amd64) ==="

# Find an ELF linker. The freestanding source (no libc) lets us cross-compile
# from macOS with Apple clang (which already emits x86_64-linux ELF objects) and
# link with LLVM lld — no Docker or cross-gcc required.
LLD=""
for c in /opt/homebrew/Cellar/lld@*/*/bin/ld.lld /opt/homebrew/opt/llvm/bin/ld.lld \
         /usr/local/opt/llvm/bin/ld.lld; do
    [ -x "$c" ] && LLD="$c" && break
done
command -v ld.lld >/dev/null 2>&1 && LLD="${LLD:-ld.lld}"

if command -v x86_64-linux-gnu-gcc >/dev/null 2>&1; then
    CC=x86_64-linux-gnu-gcc
    echo "--- using host $CC ---"
    # NOTE: no --no-undefined — the shim imports malloc (for the XVisualInfo /
    # FBConfig arrays wine XFree()s), resolved at load from the guest's libc.
    "$CC" -shared -fPIC -O2 -fvisibility=hidden -nostdlib -ffreestanding \
        -Wl,-soname,libGL.so.1 \
        -o "$SRC/libGL.so.1" "$SRC/libgl64.c"
elif command -v clang >/dev/null 2>&1 && [ -n "$LLD" ]; then
    echo "--- using host clang (x86_64-linux target) + $LLD ---"
    clang --target=x86_64-linux-gnu -fPIC -O2 -fvisibility=hidden -ffreestanding \
        -c "$SRC/libgl64.c" -o "$SRC/libgl64.o"
    "$LLD" -shared -soname libGL.so.1 \
        -o "$SRC/libGL.so.1" "$SRC/libgl64.o"
    rm -f "$SRC/libgl64.o"
else
    echo "--- compiling inside $IMAGE ---"
    docker run --rm --platform linux/amd64 -v "$SRC":/src "$IMAGE" bash -c '
        set -euo pipefail
        if ! command -v gcc >/dev/null 2>&1; then
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq && apt-get install -y -qq gcc libc6-dev
        fi
        gcc -shared -fPIC -O2 -fvisibility=hidden \
            -Wl,-soname,libGL.so.1 \
            -o /src/libGL.so.1 /src/libgl64.c
        strip --strip-unneeded /src/libGL.so.1 || true
    '
fi

echo "=== result ==="
ls -l "$SRC/libGL.so.1"
echo "--- soname + key exports ---"
if command -v readelf >/dev/null 2>&1; then
    readelf -d "$SRC/libGL.so.1" | grep -i soname || true
    readelf --dyn-syms "$SRC/libGL.so.1" | grep -E 'glXGetProcAddressARB|glXCreateContext|glXMakeCurrent|glClear' || true
fi
echo "=== done: $SRC/libGL.so.1 ==="
