"""Export Wine's architecture-independent fonts for the Wine32 data directory."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def stage_fonts(runtime):
    runtime = Path(runtime)
    source = runtime / "wine64.zip"
    archive = runtime / "glibc-rootfs32.zip"
    fonts = {}
    with zipfile.ZipFile(source) as wine:
        for entry in wine.infolist():
            if entry.filename.startswith("usr/share/wine/fonts/") and entry.filename.endswith(".ttf"):
                name = Path(entry.filename).name
                fonts["opt/wine-stable/share/wine/fonts/" + name] = wine.read(entry)
    for name in ("tahoma.ttf", "tahomabd.ttf"):
        if "opt/wine-stable/share/wine/fonts/" + name not in fonts:
            raise ValueError("Pinned Wine font is missing: " + name)

    # Preserve pinned source archives. Only the export receives extra font data.
    temporary = archive.with_suffix(".fonts.zip")
    try:
        with zipfile.ZipFile(archive) as old:
            unchanged = all(name in old.namelist() and old.read(name) == data
                            for name, data in fonts.items())
            if not unchanged:
                with zipfile.ZipFile(temporary, "w") as new:
                    new.comment = old.comment
                    for entry in old.infolist():
                        if entry.filename not in fonts:
                            new.writestr(entry, old.read(entry))
                    for name, data in sorted(fonts.items()):
                        entry = zipfile.ZipInfo(name, date_time=(2026, 10, 16, 0, 0, 0))
                        entry.compress_type = zipfile.ZIP_DEFLATED
                        entry.external_attr = 0o100644 << 16
                        new.writestr(entry, data)
        if not unchanged:
            temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)

    provenance = {
        "source_wine64_sha256": sha256(source),
        "glibc_rootfs32_sha256": sha256(archive),
        "fonts": {name: hashlib.sha256(data).hexdigest() for name, data in sorted(fonts.items())},
    }
    (runtime / "wine32-font-provenance.json").write_text(json.dumps(provenance, sort_keys=True, indent=2) + "\n")
    return provenance


if __name__ == "__main__":
    stage_fonts(sys.argv[1])
