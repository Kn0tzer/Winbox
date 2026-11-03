#!/usr/bin/env python3
"""Stage the Arial -> DejaVu Sans replacement into the 64-bit prefix.

The 32-bit stack serves the Arial family through Wine's font
replacements: tools/browser/stage-wine32-fonts.py writes
"Arial"="DejaVu Sans" into prefix32's user.reg
[Software\\Wine\\Fonts\\Replacements] at dist export. The 64-bit
prefix carried no such entry, so family-name lookups for Arial found
nothing on the 64-bit stack even though the DejaVu faces ship in
glibc-rootfs64.zip (FD-86). Blink's Windows last-resort list includes
Arial, not DejaVu Sans; Wine's replacements expose the alias to both
GDI and DirectWrite.

This is the canonical-set mirror of that mechanism, on the pattern of
stage-fonts64.sh: it rewrites web/runtime/prefix64.zip (and the dist
copy when one exists) in place, idempotently, reusing the 32-bit
script's registry edit so the two stacks cannot drift apart in shape.
The replacement target face must already ship in glibc-rootfs64.zip;
the alias is refused if it would dangle. Guarded by the
'prefix64 Arial font replacement (FD-86)' completeness check.
"""
import importlib.util
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REGISTRY_PATH = "home/username/.wine/user.reg"
TARGET_FACE = "usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"


def load_wine32_fonts():
    spec = importlib.util.spec_from_file_location(
        "stage_wine32_fonts", ROOT / "tools/browser/stage-wine32-fonts.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    fonts = load_wine32_fonts()
    stamp = subprocess.run(["date", "-u", "+%Y%m%d-%H%M%S"],
                           capture_output=True, text=True, check=True).stdout.strip()
    for runtime in (ROOT / "web/runtime", ROOT / "dist/runtime"):
        if not runtime.is_dir():
            continue
        if runtime.is_symlink():
            raise ValueError(f"stage-font-alias64: refuse symlinked runtime dir {runtime}")
        archive = runtime / "prefix64.zip"
        if not archive.is_file():
            raise ValueError(f"stage-font-alias64: expected {archive}")
        rootfs = runtime / "glibc-rootfs64.zip"
        with zipfile.ZipFile(rootfs) as z:
            if TARGET_FACE not in z.namelist():
                raise ValueError(
                    "stage-font-alias64: Arial replacement target missing: "
                    + TARGET_FACE + " in " + str(rootfs))
        with zipfile.ZipFile(archive) as z:
            registry = z.read(REGISTRY_PATH)
        if fonts.configure_replacements(registry) == registry:
            print(f"stage-font-alias64: {archive} already carries the Arial replacement - skipping")
            continue
        backup = Path("/home/ubuntu/backups") / f"winbox-font-alias64-{stamp}"
        backup.mkdir(parents=True, exist_ok=True)
        name = f"{runtime.parent.name}-{runtime.name}-{archive.name}"
        (backup / name).write_bytes(archive.read_bytes())
        fonts.update_archive(archive, {REGISTRY_PATH: fonts.configure_replacements(registry)})
        with zipfile.ZipFile(archive) as z:
            staged = z.read(REGISTRY_PATH)
        if fonts.configure_replacements(staged) != staged:
            raise ValueError(f"stage-font-alias64: final verification failed for {archive}")
        print(f"stage-font-alias64: Arial replacement staged in {archive} (backup in {backup})")


if __name__ == "__main__":
    sys.exit(main())
