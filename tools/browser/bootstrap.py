#!/usr/bin/env python3
"""Restore the exact runtime binaries recorded in runtime-assets.json."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tarfile
import tempfile
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]


def matches(path, expected):
    if not path.is_file() or path.stat().st_size != expected["bytes"]:
        return False
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest() == expected["sha256"]


def restore(manifest, archive, root=ROOT):
    """Validate the archive and every entry before replacing any cached asset."""
    if not matches(archive, manifest["archive"]):
        raise ValueError(f"archive size/SHA256 mismatch: {archive}")
    expected = {entry["path"]: entry for entry in manifest["files"]}
    with tempfile.TemporaryDirectory(prefix="bw64-assets-") as folder:
        stage = Path(folder)
        with tarfile.open(archive, "r:") as bundle:
            members = bundle.getmembers()
            if len(members) != len(expected) or {m.name for m in members} != set(expected):
                raise ValueError("archive entries differ from the pinned manifest")
            for member in members:
                relative = Path(member.name)
                if relative.is_absolute() or ".." in relative.parts or not member.isfile():
                    raise ValueError(f"unsafe archive entry: {member.name}")
                target = stage / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.extractfile(member) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
                if not matches(target, expected[member.name]):
                    raise ValueError(f"asset size/SHA256 mismatch: {member.name}")
        for name, entry in expected.items():
            destination = root / name
            if matches(destination, entry):
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
                pending = Path(output.name)
                try:
                    with (stage / name).open("rb") as source:
                        shutil.copyfileobj(source, output)
                    output.flush()
                    # These are public runtime assets; static server processes
                    # may run under a different user than the build process.
                    os.chmod(pending, 0o644)
                    os.replace(pending, destination)
                finally:
                    pending.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--local", type=Path, help="restore from a previously downloaded archive")
    parser.add_argument("--check", action="store_true", help="verify local assets without downloading")
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).with_name("runtime-assets.json")).read_text())
    missing = [entry["path"] for entry in manifest["files"] if not matches(ROOT / entry["path"], entry)]
    if not missing:
        print(f"Runtime assets verified ({len(manifest['files'])} files).")
        return
    if args.check:
        raise SystemExit("Missing or changed runtime assets: " + ", ".join(missing))
    if args.local:
        restore(manifest, args.local.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="bw64-download-") as folder:
            archive = Path(folder) / manifest["archive"]["name"]
            url = manifest["archive"]["url"]
            print(f"Downloading pinned runtime assets: {url}", flush=True)
            request = urllib.request.Request(url, headers={"User-Agent": "boxedwine64-bootstrap"})
            try:
                with urllib.request.urlopen(request, timeout=60) as response, archive.open("wb") as output:
                    shutil.copyfileobj(response, output)
            except urllib.error.HTTPError as error:
                raise SystemExit(f"Runtime asset download failed ({error.code}): {url}; use --local for an archived copy") from error
            restore(manifest, archive)
    print(f"Restored and verified {len(manifest['files'])} runtime assets.")


if __name__ == "__main__":
    main()
