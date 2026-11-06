#!/usr/bin/env bash
# stamp-wine-builtin.sh — stamp Wine's builtin signature into PE files.
#
# Why: Wine's loader only treats a PE DLL as a *builtin* (served by its
# Unix .so side) when the file carries the "Wine builtin DLL" signature
# in the DOS stub (bytes 64..96, right after the 64-byte DOS header).
# The wineserver records wine_builtin from that signature
# (server/mapping.c get_image_params); without it, find_builtin_dll
# ignores the PE file and falls back to dlopening the .so, which needs
# the __wine_spec_nt_header symbol the shipped (Debian-built, stripped)
# .so files do not export — so the load fails with
# "invalid .so library ... too old?" / STATUS_INVALID_IMAGE_FORMAT.
# That is fatal for ntdll.dll, which process startup loads only through
# the builtin path: wineboot and wine itself die (exit code 1) and the
# 32-bit guest never reaches desktop-ready.
#
# The mingw cross-builds that produce the FD-167/FD-169/FD-171 32-bit PE
# DLLs (build-wine10-*.sh) link with the mingw linker, which writes a
# plain DOS stub, so none of them carried the signature — every one of
# the 35 substituted PE DLLs in wine32.zip was affected, and the 32-bit
# boot died at ntdll.dll. Wine's own winebuild stamps the same 32 bytes
# (tools/winebuild/spec32.c make_builtin_files / output_pe_file); this
# script performs exactly that operation on already-built files:
# overwrite bytes 64..96 with the 32-byte signature (16 chars + NULs).
# The DOS stub code there is never executed for a builtin DLL (the
# Wine-built deb DLLs carry the same overwrite), and the file size,
# exports, and code are unchanged.
#
# Usage: stamp-wine-builtin.sh <pe-file> [<pe-file> ...]
# Fail-closed: a file that is not MZ/PE, or whose e_lfanew leaves no
# room for the signature (needs >= 96), aborts before anything is
# written to it. Idempotent: re-stamping a stamped file is a no-op.
set -euo pipefail
python3 - "$@" <<'PYEOF'
import struct, sys
SIG = b"Wine builtin DLL\x00" + b"\x00" * 15  # 32 bytes, winebuild's builtin_signature
assert len(SIG) == 32
for path in sys.argv[1:]:
    with open(path, "r+b") as f:
        data = f.read(1024)
        if len(data) < 256 or data[:2] != b"MZ":
            print(f"stamp-wine-builtin: {path}: not a PE file (no MZ header)", file=sys.stderr)
            sys.exit(1)
        (e_lfanew,) = struct.unpack_from("<I", data, 0x3c)
        if e_lfanew < 96:
            print(f"stamp-wine-builtin: {path}: e_lfanew {e_lfanew} < 96, no room for the Wine signature", file=sys.stderr)
            sys.exit(1)
        if data[e_lfanew:e_lfanew+4] != b"PE\x00\x00":
            print(f"stamp-wine-builtin: {path}: bad PE signature", file=sys.stderr)
            sys.exit(1)
        if data[64:96] == SIG:
            print(f"already stamped: {path}")
            continue
        f.seek(64)
        f.write(SIG)
        print(f"stamped: {path}")
PYEOF
