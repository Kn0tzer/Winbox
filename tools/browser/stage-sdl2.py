#!/usr/bin/env python3
"""Overlay staged SDL2 shared libraries into a generated runtime export."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import zipfile


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = Path(os.environ.get("BW64_SDL2_SOURCE", REPO_ROOT / "tools/rootfs64/sdl2"))
ARCHIVES = {
    "i386": ("wine32.zip", "usr/lib/i386-linux-gnu"),
    "amd64": ("wine64.zip", "usr/lib/x86_64-linux-gnu"),
}


def archive_payloads(source_root):
    payloads = {}
    for arch, (archive, library_dir) in ARCHIVES.items():
        source_dir = source_root / arch / library_dir
        files = sorted(source_dir.glob("*.so*"))
        if not files:
            raise ValueError(f"No SDL2 libraries found in {source_dir}")
        for source in files:
            if source.is_symlink() or not source.is_file():
                raise ValueError(f"SDL2 overlay must contain regular files: {source}")
            path = f"{library_dir}/{source.name}"
            payloads.setdefault(archive, {})[path] = source.read_bytes()
    return payloads


def update_archive(archive, payload):
    with zipfile.ZipFile(archive, "r") as original:
        names = [entry.filename for entry in original.infolist()]
        unchanged = all(names.count(name) == 1 and original.read(name) == data
                        for name, data in payload.items())
        if unchanged:
            return

        fd, temp_name = tempfile.mkstemp(prefix=archive.name + ".", suffix=".tmp", dir=archive.parent)
        os.close(fd)
        temporary = Path(temp_name)
        try:
            with zipfile.ZipFile(temporary, "w") as updated:
                updated.comment = original.comment
                for entry in original.infolist():
                    if entry.filename not in payload:
                        updated.writestr(entry, original.read(entry))
                for name, data in sorted(payload.items()):
                    info = zipfile.ZipInfo(name, date_time=(2023, 4, 12, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.create_system = 3
                    info.external_attr = 0o100644 << 16
                    updated.writestr(info, data)
            os.chmod(temporary, archive.stat().st_mode & 0o777)
            os.replace(temporary, archive)
        finally:
            temporary.unlink(missing_ok=True)


def stage(runtime, source_root=SOURCE_ROOT):
    runtime = Path(runtime).resolve()
    if not runtime.is_dir():
        raise ValueError(f"Runtime export directory does not exist: {runtime}")
    payloads = archive_payloads(Path(source_root).resolve())
    for name in payloads:
        if not (runtime / name).is_file():
            raise ValueError(f"Runtime archive does not exist: {runtime / name}")

    records = []
    for archive_name, payload in payloads.items():
        update_archive(runtime / archive_name, payload)
        for path, data in sorted(payload.items()):
            records.append({
                "archive": archive_name,
                "path": path,
                "sha256": hashlib.sha256(data).hexdigest(),
                "bytes": len(data),
            })
    record = runtime / "sdl2-provenance.json"
    record.write_text(json.dumps({"files": records}, sort_keys=True, indent=2) + "\n")
    return records


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path, help="generated export runtime directory")
    args = parser.parse_args()
    for item in stage(args.runtime):
        print(f"{item['archive']}: {item['path']} ({item['bytes']} bytes)")
