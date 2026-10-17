"""Export Wine32 font data and a usable DirectWrite last-resort family."""
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile


REGISTRY_PATH = "home/username/.wine/user.reg"
# Blink's Windows last-resort list includes Arial, but not DejaVu Sans.
# Wine's replacements expose the alias to both GDI and DirectWrite.
REPLACEMENTS = {"Arial": "DejaVu Sans"}


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def configure_replacements(data):
    text = data.decode("utf-8")
    if not text.startswith("WINE REGISTRY Version 2"):
        raise ValueError("Invalid Wine user registry")
    newline = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    section = r"[Software\\Wine\\Fonts\\Replacements]"
    header = re.search(r"(?m)^" + re.escape(section) + r"(?:[ \t]+[^\n]*)?$", text)
    if header:
        start = header.start()
        following = re.search(r"(?m)^\[", text[header.end():])
        end = header.end() + following.start() if following else len(text)
    else:
        text = text.rstrip("\n") + "\n\n" + section + "\n"
        start = text.rfind(section)
        end = len(text)
    block = text[start:end].rstrip("\n")
    for name, family in REPLACEMENTS.items():
        entry = f'"{name}"="{family}"'
        pattern = r'(?m)^"' + re.escape(name) + r'"=.*$'
        if re.search(pattern, block):
            block = re.sub(pattern, lambda _: entry, block)
        else:
            block += "\n" + entry
    return (text[:start] + block + "\n\n" + text[end:]).replace("\n", newline).encode("utf-8")


def update_archive(archive, payload):
    temporary = archive.with_suffix(".fonts.zip")
    try:
        with zipfile.ZipFile(archive) as old:
            unchanged = all(name in old.namelist() and old.read(name) == data
                            for name, data in payload.items())
            if not unchanged:
                with zipfile.ZipFile(temporary, "w") as new:
                    new.comment = old.comment
                    for entry in old.infolist():
                        new.writestr(entry, payload.get(entry.filename, old.read(entry)))
                    for name in sorted(payload.keys() - set(old.namelist())):
                        entry = zipfile.ZipInfo(name, date_time=(2026, 10, 16, 0, 0, 0))
                        entry.compress_type = zipfile.ZIP_DEFLATED
                        entry.external_attr = 0o100644 << 16
                        new.writestr(entry, payload[name])
        if not unchanged:
            temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)


def stage_fonts(runtime):
    runtime = Path(runtime)
    source = runtime / "wine64.zip"
    archive = runtime / "glibc-rootfs32.zip"
    prefix = runtime / "prefix32.zip"
    fonts = {}
    with zipfile.ZipFile(source) as wine:
        for entry in wine.infolist():
            if entry.filename.startswith("usr/share/wine/fonts/") and entry.filename.endswith(".ttf"):
                name = Path(entry.filename).name
                fonts["opt/wine-stable/share/wine/fonts/" + name] = wine.read(entry)
    for name in ("tahoma.ttf", "tahomabd.ttf"):
        if "opt/wine-stable/share/wine/fonts/" + name not in fonts:
            raise ValueError("Pinned Wine font is missing: " + name)
    with zipfile.ZipFile(prefix) as old:
        registry = configure_replacements(old.read(REGISTRY_PATH))
    with zipfile.ZipFile(archive) as old:
        if "usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" not in old.namelist():
            raise ValueError("Pinned Arial replacement font is missing: DejaVuSans.ttf")

    # Validate inputs before changing either derived archive. Pinned inputs stay intact.
    update_archive(archive, fonts)
    update_archive(prefix, {REGISTRY_PATH: registry})
    provenance = {
        "source_wine64_sha256": sha256(source),
        "glibc_rootfs32_sha256": sha256(archive),
        "fonts": {name: hashlib.sha256(data).hexdigest() for name, data in sorted(fonts.items())},
        "replacements": REPLACEMENTS,
        "registry_path": REGISTRY_PATH,
        "registry_sha256": hashlib.sha256(registry).hexdigest(),
    }
    (runtime / "wine32-font-provenance.json").write_text(json.dumps(provenance, sort_keys=True, indent=2) + "\n")
    return provenance


if __name__ == "__main__":
    stage_fonts(sys.argv[1])
