#!/usr/bin/env python3
"""Split large runtime ZIPs in a generated export for same-origin Pages loading."""
import argparse
import json
from pathlib import Path

CHUNK_BYTES = 90 * 1024 * 1024


def split_zip(source):
    size = source.stat().st_size
    if size <= CHUNK_BYTES:
        return
    for stale in source.parent.glob(source.name + ".part*"):
        stale.unlink()
    parts = []
    with source.open("rb") as stream:
        index = 0
        while data := stream.read(CHUNK_BYTES):
            part = source.with_name(source.name + f".part{index:03d}")
            part.write_bytes(data)
            parts.append(part.name)
            index += 1
    manifest = {"name": source.name, "totalBytes": size, "chunkBytes": CHUNK_BYTES, "parts": parts}
    source.with_name(source.name + ".manifest.json").write_text(json.dumps(manifest) + "\n")
    source.unlink()
    print(f"{source.name}: {size} bytes in {len(parts)} same-origin parts")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("export", type=Path, help="generated export directory (usually dist)")
    args = parser.parse_args()
    runtime = args.export.resolve() / "runtime"
    if not (runtime / "BUILD_INFO.json").is_file():
        raise SystemExit("Expected a generated export containing runtime/BUILD_INFO.json")
    for name in ["glibc-rootfs64.zip", "wine64.zip", "prefix64.zip", "glibc-rootfs32.zip", "wine32.zip", "prefix32.zip"]:
        split_zip(runtime / name)
    (args.export / ".nojekyll").touch()


if __name__ == "__main__":
    main()
