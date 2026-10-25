#!/bin/bash
# Build the 32-bit guest GL shim from the shared source. Keep its functions and
# capability declarations identical to libgl64; only the syscall ABI differs.
# Usage: tools/rootfs64/build-libgl32.sh [--check]
#
# Requires: python3 and an i686-linux-gnu gcc (or $GL32_CC); --check also
# needs cmp (diffutils).
set -euo pipefail
# Fail early and by name if a required tool is missing, instead of dying
# mid-run with exit 127 on a minimal box. (dirname locates this script
# before anything else runs, so it is checked first.)
command -v dirname >/dev/null || { echo "ERROR: dirname not found (install coreutils)" >&2; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
command -v python3 >/dev/null || { echo "ERROR: python3 not found (install python3)" >&2; exit 1; }
GL32_CC="${GL32_CC:-i686-linux-gnu-gcc}"
command -v "$GL32_CC" >/dev/null || { echo "ERROR: $GL32_CC not found (install gcc-i686-linux-gnu or set GL32_CC)" >&2; exit 1; }

# --check: regenerate + rebuild in a scratch copy and byte-compare BOTH the
# generated source and the binary against the committed artifacts, without
# modifying the worktree. Modelled on build_vulkan_shims.py --check;
# stage-dependencies.py runs this before staging, so a stale libgl32.c or
# libGL.so.1 cannot ship silently (the 18-export no-op-stub divergence was
# exactly a stale generated source that nothing re-verified at ship time).
if [ "${1:-}" = "--check" ]; then
    command -v cmp >/dev/null || { echo "ERROR: cmp not found (install diffutils)" >&2; exit 1; }
    SCRATCH="$(mktemp -d)"
    trap 'rm -rf "$SCRATCH"' EXIT
    mkdir -p "$SCRATCH/rootfs64"
    cp "$HERE/build-libgl32.sh" "$SCRATCH/rootfs64/build-libgl32.sh"
    cp -a "$HERE/libgl64" "$SCRATCH/rootfs64/libgl64"
    cp -a "$HERE/libgl32" "$SCRATCH/rootfs64/libgl32"
    bash "$SCRATCH/rootfs64/build-libgl32.sh" >/dev/null
    fail=0
    if ! cmp -s "$SCRATCH/rootfs64/libgl32/libgl32.c" "$HERE/libgl32/libgl32.c"; then
        echo "error: committed tools/rootfs64/libgl32/libgl32.c is not the generator output of libgl64/libgl64.c; rerun tools/rootfs64/build-libgl32.sh and commit both files" >&2
        fail=1
    fi
    if ! cmp -s "$SCRATCH/rootfs64/libgl32/libGL.so.1" "$HERE/libgl32/libGL.so.1"; then
        echo "error: committed tools/rootfs64/libgl32/libGL.so.1 does not match a fresh build of libgl32.c; rerun tools/rootfs64/build-libgl32.sh and commit the binary" >&2
        fail=1
    fi
    [ "$fail" = 0 ] || exit 1
    echo "libgl32: generated source and binary match a fresh build"
    exit 0
fi
mkdir -p "$HERE/libgl32"
python3 - "$HERE" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
s = (root / 'libgl64/libgl64.c').read_text()
start = s.index('static inline uint64_t gl64_trap(')
end = s.index('\n}', start) + 2
s = s[:start] + '''static inline uint64_t gl64_trap(uint64_t fnId, GL64Args* args) {
    uint32_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"((uint32_t)GL64_SYSCALL_NR), "b"((uint32_t)fnId),
          "c"((uint32_t)(uintptr_t)args)
        : "memory");
    return ret;
}''' + s[end:]
(root / 'libgl32/libgl32.c').write_text(s)
PY
"$GL32_CC" -shared -fPIC -O2 -fvisibility=hidden \
    -nostdlib -ffreestanding -Wl,-soname,libGL.so.1 \
    -I "$HERE/libgl64" -o "$HERE/libgl32/libGL.so.1" "$HERE/libgl32/libgl32.c"
echo "Built $HERE/libgl32/libGL.so.1"
