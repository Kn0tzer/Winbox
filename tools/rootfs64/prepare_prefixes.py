#!/usr/bin/env python3
"""Package the files written by wineboot and verify shipped Wine prefixes."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import zipfile


PREFIX = "home/username/.wine/"
HIVES = ("system.reg", "user.reg", "userdef.reg")
SERVICES = ("RpcSs", "PlugPlay", "winebus", "wineusb")
CONTROLLER_KEYS = (
    "Software\\\\Microsoft\\\\WindowsRuntime\\\\ActivatableClassId\\\\Windows.Gaming.Input.Gamepad",
    "Software\\\\Microsoft\\\\WindowsRuntime\\\\ActivatableClassId\\\\Windows.Gaming.Input.RawGameController",
    "System\\\\CurrentControlSet\\\\Enum\\\\ROOT\\\\WINE\\\\WINEBUS",
    "System\\\\CurrentControlSet\\\\Enum\\\\ROOT\\\\WINE\\\\WINEUSB",
)
RUNSERVICES_KEY = "software\\microsoft\\windows\\currentversion\\runservices"
MENU_BUILDER_VALUE = re.compile(r'^\s*"winemenubuilder"\s*=', re.IGNORECASE)


def _zip_contents(path: Path) -> tuple[dict[str, tuple[zipfile.ZipInfo, bytes]], dict[str, bytes]]:
    records: dict[str, tuple[zipfile.ZipInfo, bytes]] = {}
    contents: dict[str, bytes] = {}
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            data = archive.read(info.filename)
            records[info.filename] = (info, data)
            contents[info.filename] = data
    return records, contents


def _write_zip(path: Path, records: dict[str, tuple[zipfile.ZipInfo, bytes]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, (info, data) in records.items():
            archive.writestr(info, data)


def strip_browser_menu_runservices(data: bytes) -> tuple[bytes, int]:
    """Remove Wine's host shell menu integration from a browser prefix hive.

    wine.inf registers winemenubuilder.exe -a -r under RunServices. The browser
    has no host Start Menu to integrate, and the extra process races application
    startup on a cold prefix. Leave other RunServices values and ordinary Run
    values untouched; service registrations such as RpcSs and PlugPlay are not
    involved.
    """
    lines = data.decode("utf-8", errors="replace").splitlines(keepends=True)
    output: list[str] = []
    section = ""
    removed = 0
    for line in lines:
        stripped = line.rstrip("\r\n")
        if stripped.startswith("[") and "]" in stripped:
            section = stripped[1:stripped.index("]")].replace("\\\\", "\\").casefold()
        if section == RUNSERVICES_KEY and MENU_BUILDER_VALUE.match(stripped):
            removed += 1
            continue
        output.append(line)
    return "".join(output).encode("utf-8"), removed


def sanitize_prefix_archive(path: Path) -> int:
    """Strip browser-irrelevant menu integration from all prefix registry hives."""
    records, contents = _zip_contents(path)
    removed = 0
    for hive in HIVES:
        name = PREFIX + hive
        if name not in contents:
            continue
        data, count = strip_browser_menu_runservices(contents[name])
        if count:
            records[name] = (records[name][0], data)
            contents[name] = data
            removed += count
    if removed:
        temporary = path.with_suffix(path.suffix + ".tmp")
        try:
            _write_zip(temporary, records)
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)
    return removed


def _audio_registry(text: str) -> str:
    section = r"[Software\\Wine\\Drivers]"
    value = '"Audio"="alsa"'
    lines = text.splitlines()
    result: list[str] = []
    in_audio_section = False
    found_section = False
    wrote_value = False
    for line in lines:
        if line.startswith("["):
            if in_audio_section and not wrote_value:
                result.append(value)
                wrote_value = True
            in_audio_section = line == section or line.startswith(section + " ")
            found_section = found_section or in_audio_section
        if in_audio_section and line.lstrip().startswith('"Audio"='):
            if not wrote_value:
                result.append(value)
                wrote_value = True
            continue
        result.append(line)
    if in_audio_section and not wrote_value:
        result.append(value)
        wrote_value = True
    if not found_section:
        now = int(time.time())
        filetime = int((now + 11644473600) * 10_000_000)
        if result and result[-1]:
            result.append("")
        result.extend((section + f" {now}", f"#time={filetime:x}", value))
    return "\n".join(result) + "\n"


def _registry_sections(text: str) -> dict[str, list[str]]:
    sections: dict[str, list[str]] = {}
    key: str | None = None
    for line in text.splitlines():
        if line.startswith("[") and "]" in line:
            key = line[1:line.index("]")].lower()
            sections[key] = []
        elif key is not None:
            sections[key].append(line)
    return sections


def _validate_prefix(contents: dict[str, bytes], architecture: str,
                     require_full_init: bool = False) -> None:
    errors: list[str] = []
    expected_arch = f"#arch={architecture}".encode()
    for hive in HIVES:
        key = PREFIX + hive
        data = contents.get(key)
        if data is None:
            errors.append(f"missing {key}")
        elif b"WINE REGISTRY Version 2" not in data or expected_arch not in data:
            errors.append(f"{key} has an invalid header or architecture")

    stamp = contents.get(PREFIX + ".update-timestamp", b"").strip()
    if stamp != b"disable":
        errors.append("missing completed Wine update marker (.update-timestamp=disable)")

    for path in (
        "drive_c/windows/system32.link",
        "dosdevices/c(_colon_).link",
        "dosdevices/z(_colon_).link",
    ):
        if PREFIX + path not in contents:
            errors.append(f"missing {path}")

    system = contents.get(PREFIX + "system.reg", b"").decode("utf-8", errors="replace")
    user = contents.get(PREFIX + "user.reg", b"").decode("utf-8", errors="replace")
    system_sections = _registry_sections(system)
    for service in SERVICES:
        key = f"system\\\\currentcontrolset\\\\services\\\\{service}".lower()
        body = system_sections.get(key)
        if body is None:
            if require_full_init:
                errors.append(f"missing service registration {service}")
            continue
        match = next((re.search(r'^"Start"=dword:([0-9a-fA-F]{8})$', line) for line in body
                      if line.startswith('"Start"=')), None)
        if match is None:
            errors.append(f"service {service} has no Start value")
        elif int(match.group(1), 16) == 4:
            errors.append(f"service {service} is disabled")

    controller_keys = CONTROLLER_KEYS if require_full_init or architecture == "win32" else CONTROLLER_KEYS[:2]
    for key in controller_keys:
        if key.lower() not in system.lower():
            errors.append(f"missing controller/HID registry key {key}")
    audio_values = re.findall(r'^"Audio"="([^"]+)"$', user, re.MULTILINE)
    if audio_values and any(value != "alsa" for value in audio_values):
        errors.append("unexpected Wine audio driver setting: " + ", ".join(audio_values))

    if architecture == "win64" and '"csmt"=dword:00000000' not in user:
        errors.append("missing Direct3D csmt=0 setting")
    if architecture == "win32":
        for filename in ("d3d9.dll", "d3d11.dll", "dxgi.dll"):
            if "home/username/" + filename not in contents:
                errors.append("missing 32-bit Direct3D/DXVK payload " + filename)

    # HKLM\Software\Microsoft\Windows NT\CurrentVersion is what games and
    # installers read for ProductName/CurrentBuild/CSDVersion. wine.inf
    # writes it on every real wineboot (usr/share/wine/wine.inf [Version]
    # section: ProductName="Microsoft Windows 7", CurrentBuild=7601,
    # CSDVersion="Service Pack 1"); without it only RtlGetVersion keeps
    # working (compiled-in defaults, ntdll version_init), while direct
    # registry readers get nothing.
    if "[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion]" not in system:
        errors.append("missing Windows NT CurrentVersion version emulation section")

    if architecture == "win64" and require_full_init:
        for filename in ("winebus.inf", "winehid.inf", "wineusb.inf", "winexinput.inf"):
            key = PREFIX + "drive_c/windows/inf/" + filename
            if key not in contents:
                errors.append(f"missing installed input INF {filename}")

    if errors:
        raise ValueError("prefix validation failed: " + "; ".join(errors))


def validate_prefix_zip(path: Path, architecture: str) -> None:
    _, contents = _zip_contents(path)
    _validate_prefix(contents, architecture)


def default_support_zip() -> Path:
    """The retained support pack named by dist/runtime/dependencies.json
    (the same resolution gen_runtime_manifest.py uses)."""
    deps = json.loads((Path(__file__).resolve().parents[2]
                       / "dist/runtime/dependencies.json").read_text())
    return Path(__file__).resolve().parents[2] / "dist/runtime" / deps["archive"]


def validate_service_runtime(prefix_zip: Path, wine_zip: Path,
                             support_zip: Path | None = None) -> None:
    _, prefix = _zip_contents(prefix_zip)
    link = prefix.get(PREFIX + "drive_c/windows/system32.link")
    if link is None:
        raise ValueError(f"{prefix_zip}: missing system32.link")
    system32 = link.decode("utf-8", errors="replace").strip().strip("/") + "/"
    with zipfile.ZipFile(wine_zip) as archive:
        names = set(archive.namelist())
    required = ("winealsa.drv", "services.exe", "rpcss.exe", "plugplay.exe", "winedevice.exe",
                "winebus.sys", "winehid.sys", "wineusb.sys", "d3d11.dll", "d3d12.dll")
    # d3d12core/msmpeg2vdec/msvcp140_codecvt_ids: present in the i386
    # WineHQ-10.0 tree; backported
    # into the amd64 Debian-8.0 tree by build-wine64-zip.sh (see
    # tools/rootfs64/winehq10-x86_64/README.md). Games LoadLibrary d3d12core
    # directly and MF resolves msmpeg2vdec for WMV/H.264 cutscenes.
    if "x86_64-windows/" in system32:
        required = required + ("d3d12core.dll", "msmpeg2vdec.dll", "msvcp140_codecvt_ids.dll")
    missing = [system32 + name for name in required if system32 + name not in names]
    if missing:
        raise ValueError(f"{wine_zip}: missing linked audio/service/device/D3D payloads: " + ", ".join(missing))
    if "x86_64-windows/" in system32:
        unix_dir = system32.replace("x86_64-windows/", "x86_64-unix/")
    elif "i386-windows/" in system32:
        unix_dir = system32.replace("i386-windows/", "i386-unix/")
    else:
        unix_dir = ""
    if unix_dir:
        missing_unix = [unix_dir + name for name in ("winebus.so", "wineusb.so")
                        if unix_dir + name not in names]
        if missing_unix:
            raise ValueError(f"{wine_zip}: missing linked HID Unix modules: " + ", ".join(missing_unix))
        # winegstreamer links only the GStreamer core; without these plugins
        # (staged by prepare-gstreamer.py) every MF/DShow cutscene path
        # dead-ends at gst_element_factory_make(). Since the 2026-10-25
        # lazy split the plugin set + DT_NEEDED closure rides the support
        # pack (mounted at the same guest paths before Wine starts), NOT
        # the boot zip: a plugin in the wine zip means the split regressed,
        # and a plugin missing from the support pack means media breaks.
        lib_dir = "usr/lib/x86_64-linux-gnu/" if "x86_64-unix/" in unix_dir else "usr/lib/i386-linux-gnu/"
        if support_zip is None:
            raise ValueError("gstreamer plugin check requires the support pack: "
                             "pass support_zip (see default_support_zip())")
        with zipfile.ZipFile(support_zip) as archive:
            support_names = set(archive.namelist())
        for plugin in ("libav", "playback", "typefindfunctions", "avi", "isomp4",
                       "matroska", "ogg", "wavparse", "flac", "vorbis"):
            entry = f"{lib_dir}gstreamer-1.0/libgst{plugin}.so"
            if entry in names:
                raise ValueError(f"{wine_zip}: gstreamer plugin {entry} is in the boot zip; "
                                 "it must ride the lazy support pack instead")
            if entry not in support_names:
                raise ValueError(f"{support_zip}: missing gstreamer plugin {entry}")


def merge_registry_fragment(registry: str, fragment: str) -> tuple[str, list[str]]:
    """Append every [section] of *fragment* that *registry* still lacks.

    Registry fragments (prefix64-reg.txt, prefix-mono-reg.txt) carry state a
    real wineboot/wine-mono install writes; the pre-booted browser prefixes
    must gain it without duplicating sections on rebuild (idempotent).
    Returns the merged text and the section headers that were added.
    """
    added: list[str] = []
    for section in re.findall(r'(?ms)^(\[.*?^.*?)(?=^\[|\Z)', fragment):
        header = section.splitlines()[0].strip()
        if header.split(']')[0] + ']' not in registry:
            registry = registry.rstrip('\n') + '\n\n' + section.strip() + '\n'
            added.append(header)
    return registry, added


def patch_audio(path: Path) -> None:
    records, contents = _zip_contents(path)
    registry_path = PREFIX + "user.reg"
    if registry_path not in contents:
        raise ValueError(f"{path}: missing {registry_path}")
    data = _audio_registry(contents[registry_path].decode("utf-8", errors="replace")).encode()
    info = records[registry_path][0]
    records[registry_path] = (info, data)
    temporary = path.with_suffix(path.suffix + ".tmp")
    try:
        _write_zip(temporary, records)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def package_boot_state(base_zip: Path, boot_state: Path, output: Path) -> None:
    """Overlay a real wineboot --init result, keeping unrelated archive files."""
    if not boot_state.is_dir():
        raise FileNotFoundError(f"Wine prefix state directory not found: {boot_state}")
    records, contents = _zip_contents(base_zip)
    for hive in HIVES:
        if not (boot_state / hive).is_file():
            raise FileNotFoundError(f"booted state is missing {hive}: {boot_state}")

    for source in sorted(boot_state.rglob("*")):
        if not source.is_file():
            continue
        relative = source.relative_to(boot_state).as_posix()
        if relative == ".update-timestamp":
            continue
        name = PREFIX + relative
        old_info = records.get(name, (None, b""))[0]
        info = old_info or zipfile.ZipInfo(name)
        if old_info is None:
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
        data = source.read_bytes()
        records[name] = (info, data)
        contents[name] = data

    # Wineboot has completed; prevent each browser launch from reinstalling wine.inf.
    marker = PREFIX + ".update-timestamp"
    info = records.get(marker, (zipfile.ZipInfo(marker), b""))[0]
    records[marker] = (info, b"disable\n")
    contents[marker] = b"disable\n"
    user_reg = PREFIX + "user.reg"
    base_user = _zip_contents(base_zip)[1].get(user_reg, b"").decode("utf-8", errors="replace")
    user_text = contents[user_reg].decode("utf-8", errors="replace")
    if '"Audio"="alsa"' in base_user and '"Audio"="alsa"' not in user_text:
        user_text = _audio_registry(user_text)
    user_data = user_text.encode()
    records[user_reg] = (records[user_reg][0], user_data)
    contents[user_reg] = user_data

    for hive in HIVES:
        name = PREFIX + hive
        data, _ = strip_browser_menu_runservices(contents[name])
        records[name] = (records[name][0], data)
        contents[name] = data

    _validate_prefix(contents, "win64", require_full_init=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        _write_zip(temporary, records)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def run_native_boot(
    base_zip: Path,
    output: Path,
    boxedwine: Path,
    glibc_zip: Path,
    wine_zip: Path,
    work_dir: Path,
    timeout_seconds: int,
) -> None:
    """Run wineboot in a disposable Boxedwine root and package its written state."""
    work_dir.mkdir(parents=True, exist_ok=True)
    root = work_dir / "root"
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    records, _ = _zip_contents(base_zip)
    records.pop(PREFIX + ".update-timestamp", None)
    boot_input = work_dir / "prefix64-first-boot.zip"
    _write_zip(boot_input, records)
    log_path = work_dir / "wineboot.log"
    command = [
        str(boxedwine), "-root", str(root),
        "-zip", str(glibc_zip), "-zip", str(wine_zip), "-zip", str(boot_input),
        "-novideo",
        "-env", "HOME=/home/username",
        "-env", "USER=username",
        "-env", "WINEPREFIX=/home/username/.wine",
        "-env", "WINELOADER=/usr/lib/wine/wine64",
        "-env", "WINESERVER=/usr/lib/wine/wineserver64",
        "-env", "WINEDLLPATH=/usr/lib/x86_64-linux-gnu/wine",
        "-env", "DISPLAY=:0",
        "/usr/lib/wine/wine64", "wineboot", "--init",
    ]
    timed_out = False
    with log_path.open("wb") as log:
        try:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                           timeout=timeout_seconds, check=False)
        except subprocess.TimeoutExpired:
            timed_out = True

    state = root / "home/username/.wine"
    if not state.is_dir():
        raise RuntimeError(f"native wineboot produced no prefix at {state}; see {log_path}")
    # Validate the full state before packaging; the final disable marker is added later.
    try:
        _, contents = _zip_contents(base_zip)
        for source in state.rglob("*"):
            if source.is_file():
                name = PREFIX + source.relative_to(state).as_posix()
                contents[name] = source.read_bytes()
        contents[PREFIX + ".update-timestamp"] = b"disable\n"
        user_reg = PREFIX + "user.reg"
        base_user = _zip_contents(base_zip)[1].get(user_reg, b"").decode(errors="replace")
        user_text = contents[user_reg].decode(errors="replace")
        if '"Audio"="alsa"' in base_user and '"Audio"="alsa"' not in user_text:
            user_text = _audio_registry(user_text)
        contents[user_reg] = user_text.encode()
        _validate_prefix(contents, "win64", require_full_init=True)
    except Exception as error:
        tail = log_path.read_text(errors="replace").splitlines()[-80:]
        raise RuntimeError(f"native wineboot state failed validation: {error}; log={log_path}\n" + "\n".join(tail)) from error
    package_boot_state(base_zip, state, output)
    if timed_out:
        print(f"WARNING: Boxedwine remained running after {timeout_seconds}s; captured and validated the completed prefix writes. See {log_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    prepare = commands.add_parser("prepare", help="merge a native wineboot output tree into prefix64.zip")
    prepare.add_argument("--base", type=Path, required=True)
    prepare.add_argument("--boot-state", type=Path, required=True, help="path to the booted .wine directory")
    prepare.add_argument("--output", type=Path, required=True)

    native = commands.add_parser("native-boot", help="run wineboot under Linux Boxedwine then package its writes")
    native.add_argument("--base", type=Path, required=True)
    native.add_argument("--output", type=Path, required=True)
    native.add_argument("--boxedwine", type=Path, required=True)
    native.add_argument("--glibc-zip", type=Path, required=True)
    native.add_argument("--wine-zip", type=Path, required=True)
    native.add_argument("--work-dir", type=Path, required=True)
    native.add_argument("--timeout-seconds", type=int, default=420)

    audio = commands.add_parser("set-audio", help="set Audio=alsa in a prefix zip")
    audio.add_argument("--archive", type=Path, required=True)

    check = commands.add_parser("check", help="guard both packaged prefix architectures and linked service payloads")
    check.add_argument("--prefix32", type=Path, required=True)
    check.add_argument("--prefix64", type=Path, required=True)
    check.add_argument("--wine32", type=Path, required=True)
    check.add_argument("--wine64", type=Path, required=True)
    check.add_argument("--support", type=Path,
                       help="support pack carrying the gstreamer closure "
                            "(default: the pack named by dist/runtime/dependencies.json)")

    check_one = commands.add_parser("check-one", help="validate one initialized prefix archive")
    check_one.add_argument("--archive", type=Path, required=True)
    check_one.add_argument("--arch", choices=("win32", "win64"), required=True)

    sanitize = commands.add_parser("sanitize", help="remove browser-irrelevant winemenubuilder autostart from a prefix archive")
    sanitize.add_argument("--archive", type=Path, required=True)

    args = parser.parse_args()
    if args.command == "prepare":
        package_boot_state(args.base, args.boot_state, args.output)
        print(f"Prepared initialized 64-bit prefix: {args.output}")
    elif args.command == "native-boot":
        run_native_boot(args.base, args.output, args.boxedwine, args.glibc_zip,
                        args.wine_zip, args.work_dir, args.timeout_seconds)
        print(f"Prepared initialized 64-bit prefix: {args.output}")
    elif args.command == "set-audio":
        patch_audio(args.archive)
        print(f"Set ALSA audio driver: {args.archive}")
    elif args.command == "check":
        validate_prefix_zip(args.prefix32, "win32")
        validate_prefix_zip(args.prefix64, "win64")
        support = args.support if args.support is not None else default_support_zip()
        validate_service_runtime(args.prefix32, args.wine32, support)
        validate_service_runtime(args.prefix64, args.wine64, support)
        print("Both preinitialized Wine prefixes passed package checks.")
    elif args.command == "check-one":
        validate_prefix_zip(args.archive, args.arch)
        print(f"Preinitialized {args.arch} prefix passed package checks.")
    elif args.command == "sanitize":
        removed = sanitize_prefix_archive(args.archive)
        print(f"Removed {removed} winemenubuilder RunServices value(s) from {args.archive}.")


if __name__ == "__main__":
    main()
