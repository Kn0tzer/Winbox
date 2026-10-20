#!/usr/bin/env python3
"""Ensure the prebooted 64-bit Wine prefix starts its SDL-capable WineBus."""
import argparse
import os
from pathlib import Path
import tempfile
import zipfile


PREFIX_MEMBER = "home/username/.wine/system.reg"
REGISTRY_SECTIONS = (
    r'''[System\\CurrentControlSet\\Control\\Class\\{4d36e97d-e325-11ce-bfc1-08002be10318}] 1791272934
#time=1dd556722a11766
@="System devices"
"Class"="System"

[System\\CurrentControlSet\\Control\\Class\\{4d36e97d-e325-11ce-bfc1-08002be10318}\\0000] 1791272933
#time=1dd556722615b44
"InfPath"="winebus.inf"
"InfSection"="device_section"''',
    r'''[System\\CurrentControlSet\\Enum\\ROOT\\WINE\\WINEBUS] 1791272933
#time=1dd5567225fa646
"Class"="System"
"ClassGUID"="{4D36E97D-E325-11CE-BFC1-08002BE10318}"
"ConfigFlags"=dword:00000000
"DeviceDesc"="Wine HID bus driver"
"Driver"="{4D36E97D-E325-11CE-BFC1-08002BE10318}\\0000"
"HardwareId"=hex(7):72,00,6f,00,6f,00,74,00,5c,00,77,00,69,00,6e,00,65,00,62,\\
  00,75,00,73,00,00,00,00,00,43,00,3a,00,5c,00,77,00,69,00,6e,00,64,00,6f,00,\\
  77,00,73,00,5c,00,69,00,6e,00,66,00
"Service"="winebus"

[System\\CurrentControlSet\\Enum\\ROOT\\WINE\\WINEBUS\\Device Parameters] 1791272933
#time=1dd55672257d402''',
    r'''[System\\CurrentControlSet\\Services\\winebus] 1791272933
#time=1dd5567225e050c
"Description"="Wine HID bus driver"
"DisplayName"="Wine HID bus"
"ErrorControl"=dword:00000001
"Group"="WinePlugPlay"
"ImagePath"="C:\\windows\\system32\\drivers\\winebus.sys"
"ObjectName"="LocalSystem"
"PreshutdownTimeout"=dword:0002bf20
"Start"=dword:00000003
"Type"=dword:00000001''',
)


def patch_registry(data):
    text = data.decode("utf-8")
    lowered = text.casefold()
    missing = [section for section in REGISTRY_SECTIONS
               if section.splitlines()[0].split("]", 1)[0][1:].casefold() not in lowered]
    if not missing:
        return data
    return (text.rstrip() + "\n\n" + "\n\n".join(missing) + "\n").encode("utf-8")


def stage(runtime):
    runtime = Path(runtime).resolve()
    prefix = runtime / "prefix64.zip"
    if not prefix.is_file():
        raise ValueError(f"64-bit prefix archive does not exist: {prefix}")
    with zipfile.ZipFile(prefix, "r") as original:
        if original.namelist().count(PREFIX_MEMBER) != 1:
            raise ValueError(f"Expected one {PREFIX_MEMBER} entry in {prefix}")
        old_registry = original.read(PREFIX_MEMBER)
        new_registry = patch_registry(old_registry)
        if new_registry == old_registry:
            return False
        fd, temp_name = tempfile.mkstemp(prefix=prefix.name + ".", suffix=".tmp", dir=prefix.parent)
        os.close(fd)
        temporary = Path(temp_name)
        try:
            with zipfile.ZipFile(temporary, "w") as updated:
                updated.comment = original.comment
                for entry in original.infolist():
                    if entry.filename != PREFIX_MEMBER:
                        updated.writestr(entry, original.read(entry))
                info = zipfile.ZipInfo(PREFIX_MEMBER, date_time=(2026, 10, 20, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                updated.writestr(info, new_registry)
            os.chmod(temporary, prefix.stat().st_mode & 0o777)
            os.replace(temporary, prefix)
        finally:
            temporary.unlink(missing_ok=True)
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path, help="generated export runtime directory")
    args = parser.parse_args()
    changed = stage(args.runtime)
    print(f"prefix64.zip: WineBus SDL registry {'added' if changed else 'already present'}")
