#!/bin/bash
# build-prefix64.sh — (re)assemble the initialized wine prefix overlay that the
# browser launcher mounts as a third zip (see project/emscripten/wine64-launcher.js,
# ?p=<prog>). This script preserves preinitialized state in its base archive;
# `WINBOX_PREFIX64_BOOT_STATE` can merge a captured native wineboot result, and
# `WINBOX_PREFIX64_FORCE_BOOT=1` requests a fresh native init explicitly.
#
# Why this script exists: the original prefix64.zip was staged ad-hoc and was
# missing the C:\users\username user-profile directory tree. wine's registry
# (user.reg "Shell Folders" / system.reg ProfileList "ProfilesDirectory"="C:\users")
# points Desktop/Personal(Documents)/AppData/etc. at C:\users\username\..., and
# %USERPROFILE% resolves to C:\users\username. With those dirs ABSENT, comdlg32's
# GetSaveFileName / shell32 IShellFolder enumeration found nothing — Notepad's
# "Save As" dialog showed a GRAY/empty file list and Save silently failed.
# Normally wineboot --init creates this tree, but this prefix is pre-booted and
# never runs wineboot, so we materialize the dirs here.
#
# The prefix maps c: -> ../drive_c (i.e. home/username/.wine/drive_c). We add
# empty profile dirs under there; `zip` stores empty directories fine and the
# kernel overlays a writable native root on top so Save can actually write.
#
# Idempotent: takes WINBOX_PREFIX64_BASE_ZIP when supplied, otherwise starts
# from web/runtime/prefix64.zip, adds the profile tree, and writes
# tools/rootfs64/dist/prefix64.zip (gitignored build artifact). It copies the
# result to project/emscripten/Build/Wasm64Mt/ when that local Build directory
# exists.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DIST="$HERE/dist"
ZIP="$DIST/prefix64.zip"
ROOT="$(cd "$HERE/../.." && pwd)"
RUNTIME="$ROOT/web/runtime"
PREFIX_HELPER="$HERE/prepare_prefixes.py"
DEPLOY="$HERE/../../project/emscripten/Build/Wasm64Mt/prefix64.zip"
RUNTIME_INPUT="$RUNTIME"
if [ -d "$ROOT/dist/runtime" ]; then RUNTIME_INPUT="$ROOT/dist/runtime"; fi

mkdir -p "$DIST"
BASE_ZIP="${WINBOX_PREFIX64_BASE_ZIP:-$RUNTIME/prefix64.zip}"
[ -f "$BASE_ZIP" ] || { echo "ERROR: base prefix not found at $BASE_ZIP" >&2; exit 1; }

# drive_c is rooted under the .wine prefix dir inside the zip.
PREFIX_IN_ZIP="home/username/.wine"
DRIVE_C="$PREFIX_IN_ZIP/drive_c"

# The standard Wine user-profile tree (what wineboot --init would create for the
# "username" profile), plus the Public profile shell32 also references. Paths are
# relative to drive_c (== C:\). These match the registry Shell Folders targets.
PROFILE_DIRS=(
  users/username/Desktop
  users/username/Documents
  users/username/Downloads
  users/username/Music
  users/username/Pictures
  users/username/Videos
  users/username/Favorites
  users/username/Links
  users/username/Contacts
  users/username/Searches
  users/username/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Administrative Tools
  users/username/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/StartUp
  users/username/AppData/Roaming/Microsoft/Windows/Network Shortcuts
  users/username/AppData/Roaming/Microsoft/Windows/Printer Shortcuts
  users/username/AppData/Roaming/Microsoft/Windows/Recent
  users/username/AppData/Roaming/Microsoft/Windows/SendTo
  users/username/AppData/Roaming/Microsoft/Windows/Templates
  users/username/AppData/Local/Microsoft/Windows/INetCache
  users/username/AppData/Local/Microsoft/Windows/INetCookies
  users/username/AppData/Local/Microsoft/Windows/History
  users/username/AppData/Local/Temp
  users/Public/Desktop
  users/Public/Documents
  users/Public/Music
  users/Public/Pictures
  users/Public/Videos
)

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "=== unpacking base prefix64.zip ==="
unzip -q "$BASE_ZIP" -d "$STAGE"

echo "=== materializing C:\\users profile tree under $DRIVE_C ==="
for d in "${PROFILE_DIRS[@]}"; do
  mkdir -p "$STAGE/$DRIVE_C/$d"
done

# Bundle the extra GL demo executables the app launcher offers (glcube.exe is
# already in the base zip). The launcher's app bar (?p=<prog> / launchApp) runs
# these against this pre-booted prefix, so they must live both at the HOME root
# (Z:\home\username\<prog>) and under drive_c (C:\<prog>), mirroring glcube.exe.
GLTEST="$HERE/gltest"
for exe in gltri.exe d3dtri.exe deskpin.exe soundtest.exe; do
  if [ -f "$GLTEST/$exe" ]; then
    cp "$GLTEST/$exe" "$STAGE/home/username/$exe"
    cp "$GLTEST/$exe" "$STAGE/$DRIVE_C/$exe"
    echo "  + bundled $exe"
  else
    echo "  NOTE: $GLTEST/$exe missing — app-bar button for it will ENOENT." >&2
  fi
done

# Bundle the self-contained games the app launcher's "Games" row offers. These
# are built from tools/rootfs64/games/src (Win32/GDI, mingw-w64 -> 64-bit PE; see
# games/build-games.sh) plus the freely-redistributable shareware doom1.wad.
# Same placement: HOME root + drive_c. doom.exe needs doom1.wad next to it AND is
# launched with `-iwad Z:\home\username\doom1.wad` (see the app bar in wine64.html).
GAMES="$HERE/games"
# clipset/clipget are not games — they're the browser<->guest clipboard bridge
# helpers (games/src/clipset.c, clipget.c) — but they ride the same staging.
for f in snake.exe tetris.exe doom.exe doom1.wad clipset.exe clipget.exe; do
  if [ -f "$GAMES/$f" ]; then
    cp "$GAMES/$f" "$STAGE/home/username/$f"
    cp "$GAMES/$f" "$STAGE/$DRIVE_C/$f"
    echo "  + bundled $f"
  else
    echo "  NOTE: $GAMES/$f missing — app-bar button for it will ENOENT." >&2
  fi
done
# Bundle larger third-party utility apps from tools/rootfs64/apps/<Name>/ as WHOLE
# packages (the app bar launches one exe from inside). HxD (hex editor) is a
# native Win32/GDI app whose license REQUIRES redistributing the ORIGINAL PACKAGE
# INTACT — no file removed or modified — so we copy the entire extracted folder
# (HxD64.exe + HxD32.exe + HxD.exe + license.txt/readme/changelog), not just the
# exe. Launched via `HxD\HxD64.exe` (the 64-bit build) from the HOME root.
APPS="$HERE/apps"
for pkg in HxD; do
  if [ -d "$APPS/$pkg" ]; then
    # REPLACE, don't merge: the stage starts from the PREVIOUS prefix64.zip
    # (idempotent-by-rebuild), so the package dir already exists there — and
    # `cp -R src dst` with an existing dst copies INTO it, nesting HxD/HxD/...
    # one level deeper per rebuild (observed: the zip grew 18MB -> 30MB).
    rm -rf "$STAGE/home/username/$pkg" "$STAGE/$DRIVE_C/$pkg"
    cp -R "$APPS/$pkg" "$STAGE/home/username/$pkg"
    cp -R "$APPS/$pkg" "$STAGE/$DRIVE_C/$pkg"
    echo "  + bundled $pkg package ($(ls "$APPS/$pkg" | wc -l | tr -d ' ') files, intact per license)"
  else
    echo "  NOTE: $APPS/$pkg missing — app-bar button for it will ENOENT." >&2
  fi
done

# Drop a .keep in each leaf so the dir survives any tooling that prunes empties,
# and so Save As shows a non-empty, browsable target. (zip stores empty dirs, but
# the marker also makes the populated tree visible when debugging the VFS.)
find "$STAGE/$DRIVE_C/users" -type d -empty -exec touch '{}/.keep' \;

# === Direct3D: disable wined3d's multithreaded command stream (CSMT) ===========
# wined3d 8.0 defaults cs_multithreaded = WINED3D_CSMT_ENABLE. With CSMT on it
# spawns a worker thread (wined3d_cs_run) and Present() submits ops to a queue +
# spins (wined3d_cs_mt_finish / the pending_presents latency loop) until the
# worker drains them. On our backend that worker thread was never created for the
# render process, so Present() busy-waited forever (zero GL traps — it parked
# before doing any GL). Setting csmt=0 clears WINED3D_CSMT_ENABLE so wined3d keeps
# the inline single-threaded ops (wined3d_cs_st_*): submit runs the op handler on
# the CALLING thread immediately and finish is a no-op, so Present executes
# glClear/glDrawArrays/SwapBuffers synchronously with no worker thread to wait on.
# Verified against wine-8.0 dlls/wined3d/cs.c (wined3d_cs_create gate at L3423-3435,
# wined3d_cs_st_submit at L2924, wined3d_main.c reads "csmt" DWORD under
# HKCU\Software\Wine\Direct3D). Belt-and-suspenders vs the GL-extension fence gate
# (we also don't advertise GL_ARB_sync/NV_fence/APPLE_fence).
USER_REG="$STAGE/$PREFIX_IN_ZIP/user.reg"
if [ -f "$USER_REG" ] && ! grep -q '^\[Software\\\\Wine\\\\Direct3D\]' "$USER_REG"; then
  echo "=== injecting [Software\\Wine\\Direct3D] csmt=0 into user.reg ==="
  cat >> "$USER_REG" <<'REGEOF'

[Software\\Wine\\Direct3D] 1780332434
#time=1dcf1e649929046
"csmt"=dword:00000000
REGEOF
else
  echo "=== user.reg already has [Software\\Wine\\Direct3D] (or no user.reg) — skipping csmt inject ==="
fi

echo "=== rezipping prefix64.zip ==="
rm -f "$ZIP"
( cd "$STAGE" && zip -qry9 "$ZIP" . )

# A real wineboot pass can install wine.inf's profile, services, PnP, INF and
# WinSxS state. The 32-bit prefix is produced by tools/build-prefix32.sh. For
# 64-bit builds, preserve a captured native init state when explicitly supplied;
# a fresh native pass is opt-in because the current headless services run is not
# reliable enough to force into every browser prefix build.
if [ -n "${WINBOX_PREFIX64_BOOT_STATE:-}" ]; then
  python3 "$PREFIX_HELPER" prepare \
    --base "$ZIP" \
    --boot-state "$WINBOX_PREFIX64_BOOT_STATE" \
    --output "$ZIP"
elif [ "${WINBOX_PREFIX64_FORCE_BOOT:-0}" = "1" ]; then
  BOXEDWINE="${BW64_NATIVE_BOXEDWINE:-$ROOT/project/linux/Build/X64Headless/boxedwine}"
  python3 "$PREFIX_HELPER" native-boot \
    --base "$ZIP" \
    --output "$ZIP" \
    --boxedwine "$BOXEDWINE" \
    --glibc-zip "${BW64_GLIBC64_ZIP:-$RUNTIME_INPUT/glibc-rootfs64.zip}" \
    --wine-zip "${BW64_WINE64_ZIP:-$RUNTIME_INPUT/wine64.zip}" \
    --work-dir "${WINBOX_PREFIX64_WORK:-/tmp/winbox-finish/r2/work-wake/native-prefix64}" \
    --timeout-seconds "${WINBOX_PREFIX64_TIMEOUT_S:-420}"
elif python3 "$PREFIX_HELPER" check-one --archive "$ZIP" --arch win64; then
  echo "=== base prefix already contains completed wineboot state ==="
else
  echo "ERROR: prefix lacks required hives, links, or update marker; supply WINBOX_PREFIX64_BOOT_STATE or set WINBOX_PREFIX64_FORCE_BOOT=1" >&2
  exit 1
fi

# The 32-bit builder runs native wineboot twice. Preserve any existing audio
# setting in its archive; do not add one here because an explicit ALSA override
# increased click-to-mapped time in the browser measurements.
PREFIX32_ZIP="${WINBOX_PREFIX32_ZIP:-$RUNTIME/prefix32.zip}"
if [ -z "${WINBOX_PREFIX32_ZIP:-}" ] && [ -f "$ROOT/dist/runtime/prefix32.zip" ]; then
  PREFIX32_ZIP="$ROOT/dist/runtime/prefix32.zip"
fi
python3 "$PREFIX_HELPER" check \
  --prefix32 "$PREFIX32_ZIP" \
  --prefix64 "$ZIP" \
  --wine32 "${BW64_WINE32_ZIP:-$RUNTIME_INPUT/wine32.zip}" \
  --wine64 "${BW64_WINE64_ZIP:-$RUNTIME_INPUT/wine64.zip}"

echo "--- new prefix64.zip ---"
ls -lh "$ZIP"
echo "--- profile tree in zip ---"
unzip -l "$ZIP" | grep -i 'users/' | sed -n '1,40p'

if [ ! -e "$DEPLOY" ] && [ ! -L "$DEPLOY" ]; then
  if [ -d "$(dirname "$DEPLOY")" ]; then
    echo "=== deploying to $DEPLOY ==="
    cp "$ZIP" "$DEPLOY"
  else
    echo "NOTE: $(dirname "$DEPLOY") not present — skipped deploy." >&2
  fi
elif [ "$ZIP" -ef "$DEPLOY" ]; then
  # Build/ prefix64.zip is a symlink back to dist/ — deploy is automatic.
  echo "=== Build/ prefix64.zip -> dist (symlink); deploy is automatic ==="
else
  echo "=== deploying to $DEPLOY ==="
  cp "$ZIP" "$DEPLOY"
fi
echo "=== done ==="
