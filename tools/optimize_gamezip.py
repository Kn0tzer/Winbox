#!/usr/bin/env python3
"""Write an identical ZIP_STORED game archive for direct HTTP Range reads.

Usage: optimize_gamezip.py source.zip output.zip
The source is never modified. The output is replaced atomically only after
all entries have been streamed and their sizes/CRCs have been verified.
"""
import argparse
import copy
import os
from pathlib import Path
import shutil
import tempfile
import zipfile


def optimize(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    if source == output:
        raise ValueError("Output must differ from the source archive")
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".gamezip-", suffix=".zip", dir=output.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(source) as original, zipfile.ZipFile(temporary, "w", allowZip64=True) as stored:
            expected = []
            stored.comment = original.comment
            for entry in original.infolist():
                expected.append((entry.filename, entry.file_size, entry.CRC))
                info = copy.copy(entry)
                info.compress_type = zipfile.ZIP_STORED
                with original.open(entry) as reader, stored.open(info, "w", force_zip64=True) as writer:
                    shutil.copyfileobj(reader, writer, length=1024 * 1024)
        with zipfile.ZipFile(temporary) as check:
            actual = [(e.filename, e.file_size, e.CRC) for e in check.infolist()]
            if actual != expected or any(e.compress_type != zipfile.ZIP_STORED for e in check.infolist()):
                raise ValueError("Stored archive failed its entry size/CRC check")
        os.replace(temporary, output)
        print(f"Stored {len(expected)} entries: {source.stat().st_size} -> {output.stat().st_size} bytes", flush=True)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("output")
    args = parser.parse_args()
    optimize(args.source, args.output)
