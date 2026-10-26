#!/bin/bash
# Build the 32-bit winmm.dll bypass shim from winmm.c (routes guest waveOut
# directly to Z:\dev\dsp; see winmm.c). Keep its functions identical to the
# 64-bit audio path's contract; only the DLL surface matters here.
# Usage: tools/winmm32/build-winmm.sh [--check]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CC="${WINMM_CC:-i686-w64-mingw32-gcc}"

# Provenance (recorded 2026-10-25 by the remaining-artifact-ties lane; before
# that NO build recipe existed anywhere for this binary):
#   i686-w64-mingw32-gcc -shared winmm.c -o winmm.dll     (default flags)
# A fresh build with exactly that command reproduces the committed binary
# (86,666 bytes) bit-for-bit except 8 bytes in three build-time fields: the
# COFF TimeDateStamp, the PE CheckSum (computed over the timestamp), and the
# CodeView debug-directory TimeDateStamp. The section-size table of the
# committed binary matches a DEFAULT (unoptimized) build exactly, so do NOT
# add -O flags: at -O2 gcc inlines the memset call in waveOutGetDevCapsA,
# dropping memset from the import table and shrinking .text — a different,
# guest-visible artifact.
#
# --check: rebuild in a scratch dir and require (a) the same export set and
# (b) byte equality after masking those three build-time fields, without
# modifying the worktree. Modelled on tools/rootfs64/build-libgl32.sh
# --check. A byte-exact compare is impossible for PE outputs (linkers stamp
# the build time), which is why the mask exists; everything outside the
# three fields is compared exactly.
if [ "${1:-}" = "--check" ]; then
    SCRATCH="$(mktemp -d)"
    trap 'rm -rf "$SCRATCH"' EXIT
    cp "$HERE/winmm.c" "$SCRATCH/winmm.c"
    ( cd "$SCRATCH" && "$CC" -shared winmm.c -o winmm.dll )
    python3 - "$SCRATCH/winmm.dll" "$HERE/winmm.dll" <<'PY'
import struct, sys

def parse(path):
    b = open(path, 'rb').read()
    assert b[:2] == b'MZ', path
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    assert b[pe:pe+4] == b'PE\0\0', path
    nsec = struct.unpack_from('<H', b, pe+6)[0]
    optsz = struct.unpack_from('<H', b, pe+20)[0]
    opt = pe + 24
    assert struct.unpack_from('<H', b, opt)[0] == 0x10b, 'expected PE32'
    secs = []
    for i in range(nsec):
        s = opt + optsz + i * 40
        vsz, va, rsz, roff = struct.unpack_from('<IIII', b, s + 8)
        secs.append((va, vsz, roff, rsz))
    def r2o(rva):
        for va, vsz, roff, rsz in secs:
            if va <= rva < va + max(vsz, rsz):
                return roff + (rva - va)
        raise ValueError('rva %#x not in any section' % rva)
    # Build-time fields: COFF TimeDateStamp and the PE CheckSum over it.
    masked = [(pe + 8, 4), (opt + 64, 4)]
    dd = opt + 96  # data directories (PE32)
    dbg_rva, dbg_size = struct.unpack_from('<II', b, dd + 6 * 8)
    if dbg_rva:
        base = r2o(dbg_rva)
        for i in range(dbg_size // 28):
            masked.append((base + i * 28 + 4, 4))  # debug TimeDateStamp
    exp_rva, _ = struct.unpack_from('<II', b, dd)
    eo = r2o(exp_rva)
    masked.append((eo + 4, 4))  # export-directory TimeDateStamp
    n_names = struct.unpack_from('<I', b, eo + 24)[0]
    no = r2o(struct.unpack_from('<I', b, eo + 32)[0])
    exports = []
    for i in range(n_names):
        o = r2o(struct.unpack_from('<I', b, no + i * 4)[0])
        exports.append(b[o:b.index(b'\0', o)].decode())
    return b, exports, masked

def masked_bytes(b, ranges):
    b = bytearray(b)
    for off, ln in ranges:
        b[off:off + ln] = b'\0' * ln
    return bytes(b)

fresh_b, fresh_exp, fresh_masked = parse(sys.argv[1])
committed_b, committed_exp, committed_masked = parse(sys.argv[2])
fail = 0
if sorted(fresh_exp) != sorted(committed_exp):
    print('error: export set of a fresh build differs from the committed winmm.dll', file=sys.stderr)
    print('  fresh:     %s' % sorted(fresh_exp), file=sys.stderr)
    print('  committed: %s' % sorted(committed_exp), file=sys.stderr)
    fail = 1
a = masked_bytes(fresh_b, fresh_masked)
c = masked_bytes(committed_b, committed_masked)
if len(a) != len(c) or a != c:
    first = next((i for i in range(min(len(a), len(c))) if a[i] != c[i]), min(len(a), len(c)))
    print('error: committed winmm.dll differs from a fresh build beyond the PE build-time fields '
          '(first difference at byte %d); rerun tools/winmm32/build-winmm.sh and commit the binary, '
          'or fix winmm.c' % first, file=sys.stderr)
    fail = 1
sys.exit(fail)
PY
    echo "winmm32: export set and bytes (modulo PE build-time fields) match a fresh build"
    exit 0
fi

# Compile via a relative source path from inside the directory: the
# committed binary's debug info records the argv path 'winmm.c'; an
# absolute path would change bytes outside the build-time fields.
( cd "$HERE" && "$CC" -shared winmm.c -o winmm.dll )
echo "Built $HERE/winmm.dll"
