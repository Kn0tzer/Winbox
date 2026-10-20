#!/usr/bin/env python3
"""Ensure a Wine prefix selects the ALSA audio driver."""

from pathlib import Path
import sys
import time


def set_audio_driver(registry: Path) -> None:
    if not registry.is_file():
        raise FileNotFoundError(f"Wine user registry not found: {registry}")

    section = r"[Software\\Wine\\Drivers]"
    audio_value = '"Audio"="alsa"'
    lines = registry.read_text().splitlines()
    output = []
    in_section = False
    section_found = False
    audio_written = False

    for line in lines:
        if line.startswith("["):
            if in_section and not audio_written:
                output.append(audio_value)
                audio_written = True
            in_section = line == section or line.startswith(section + " ")
            section_found = section_found or in_section
        if in_section and line.lstrip().startswith('"Audio"='):
            if not audio_written:
                output.append(audio_value)
                audio_written = True
            continue
        output.append(line)

    if in_section and not audio_written:
        output.append(audio_value)
        audio_written = True
    if not section_found:
        now = time.time()
        win_filetime = int((now + 11644473600) * 10_000_000)
        if output and output[-1]:
            output.append("")
        output.extend((
            f"{section} {int(now)}",
            f"#time={win_filetime:x}",
            audio_value,
        ))

    with registry.open("w", newline="\n") as target:
        target.write("\n".join(output) + "\n")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} USER_REG")
    set_audio_driver(Path(sys.argv[1]))
