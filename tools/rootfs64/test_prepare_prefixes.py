#!/usr/bin/env python3
"""Unit tests for Wine prefix packaging and the preinitialization guard."""

from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_prefixes as prefixes


def registry(architecture: str, user: bool = False) -> bytes:
    header = f"WINE REGISTRY Version 2\n#arch={architecture}\n\n"
    if user:
        return (header + '[Software\\\\Wine\\\\Direct3D] 1\n"csmt"=dword:00000000\n'
                '[Software\\\\Wine\\\\Drivers] 1\n"Audio"="alsa"\n').encode()
    lines = [header.rstrip()]
    for service, start in (("RpcSs", "00000003"), ("PlugPlay", "00000002"),
                           ("winebus", "00000003"), ("wineusb", "00000003")):
        lines.extend((f"[System\\\\CurrentControlSet\\\\Services\\\\{service}] 1",
                      f'"Start"=dword:{start}', ""))
    lines.extend((r"[Software\\Microsoft\\Windows NT\\CurrentVersion] 1",
                  r'"ProductName"="Microsoft Windows 7"', r'"CurrentBuild"="7601"',
                  r'"CSDVersion"="Service Pack 1"', ""))
    lines.extend((r"[Software\\Microsoft\\Windows\\CurrentVersion\\RunServices] 1",
                  r'"winemenubuilder"="C:\\windows\\system32\\winemenubuilder.exe -a -r"', ""))
    lines.extend((
        r"[Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.Gaming.Input.Gamepad] 1",
        r"[Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.Gaming.Input.RawGameController] 1",
        r"[System\\CurrentControlSet\\Enum\\ROOT\\WINE\\WINEBUS] 1",
        r"[System\\CurrentControlSet\\Enum\\ROOT\\WINE\\WINEUSB] 1",
        '"Class"="HIDClass"',
    ))
    return ("\n".join(lines) + "\n").encode()


def valid_entries(architecture: str) -> dict[str, bytes]:
    root = prefixes.PREFIX
    entries = {
        root + ".update-timestamp": b"disable\n",
        root + "system.reg": registry(architecture),
        root + "user.reg": registry(architecture, user=True),
        root + "userdef.reg": registry(architecture),
        root + "drive_c/windows/system32.link": b"/wine/system32",
        root + "dosdevices/c(_colon_).link": b"../drive_c",
        root + "dosdevices/z(_colon_).link": b"/",
    }
    if architecture == "win32":
        for filename in ("d3d9.dll", "d3d11.dll", "dxgi.dll"):
            entries["home/username/" + filename] = (filename + " payload").encode()
    else:
        for name in ("winebus.inf", "winehid.inf", "wineusb.inf", "winexinput.inf"):
            entries[root + "drive_c/windows/inf/" + name] = b"inf"
    return entries


def without_audio_setting(data: bytes) -> bytes:
    text = data.decode()
    text = text.replace('[Software\\\\Wine\\\\Drivers] 1\n"Audio"="alsa"\n', "")
    return text.encode()


class PrefixPackagingTests(unittest.TestCase):
    def test_audio_setting_replaces_duplicates_and_preserves_direct3d(self):
        text = ('WINE REGISTRY Version 2\n#arch=win64\n\n'
                '[Software\\\\Wine\\\\Drivers] 1\n"Audio"="oss"\n"Video"="x11"\n'
                '[Software\\\\Wine\\\\Direct3D] 1\n"csmt"=dword:00000000\n')
        patched = prefixes._audio_registry(text)
        self.assertEqual(patched.count('"Audio"="alsa"'), 1)
        self.assertNotIn('"Audio"="oss"', patched)
        self.assertIn('"Video"="x11"', patched)
        self.assertIn('"csmt"=dword:00000000', patched)
        self.assertEqual(prefixes._audio_registry(patched).count('"Audio"="alsa"'), 1)

    def test_browser_prefix_removes_only_winemenubuilder_runservices_entry(self):
        original = (b'WINE REGISTRY Version 2\n#arch=win32\n\n'
                    b'[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\RunServices] 1\n'
                    b'"winemenubuilder"="C:\\\\windows\\\\system32\\\\winemenubuilder.exe -a -r"\n'
                    b'"other-service"="C:\\\\windows\\\\system32\\\\keep.exe"\n\n'
                    b'[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run] 1\n'
                    b'"winemenubuilder"="leave this unrelated key alone"\n')
        patched, removed = prefixes.strip_browser_menu_runservices(original)
        self.assertEqual(removed, 1)
        self.assertNotIn(b'"winemenubuilder"=', patched.split(b'[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run]')[0])
        self.assertIn(b'"other-service"="C:\\\\windows\\\\system32\\\\keep.exe"', patched)
        self.assertIn(b'"winemenubuilder"="leave this unrelated key alone"', patched)

    def test_prefix_archive_sanitizer_changes_only_the_browser_runservice_value(self):
        with tempfile.TemporaryDirectory() as temp:
            archive_path = Path(temp) / "prefix32.zip"
            entries = valid_entries("win32")
            with zipfile.ZipFile(archive_path, "w") as archive:
                for name, data in entries.items():
                    archive.writestr(name, data)
            self.assertEqual(prefixes.sanitize_prefix_archive(archive_path), 2)
            with zipfile.ZipFile(archive_path) as archive:
                system = archive.read(prefixes.PREFIX + "system.reg")
                self.assertNotIn(b'"winemenubuilder"=', system)
                self.assertIn(b'"Start"=dword:00000003', system)
                self.assertIn(b"RpcSs", system)
                self.assertIn(b"PlugPlay", system)
                self.assertIn(b"WINEBUS", system)
                self.assertIn(b"WINEUSB", system)
                self.assertEqual(archive.read(prefixes.PREFIX + ".update-timestamp"), b"disable\n")
            self.assertEqual(prefixes.sanitize_prefix_archive(archive_path), 0)

    def test_boot_overlay_adds_state_without_dropping_app_payloads(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            base_zip = temp / "base.zip"
            out_zip = temp / "prepared.zip"
            state = temp / "booted-prefix"
            state.mkdir()
            base_entries = valid_entries("win64")
            base_entries.pop(prefixes.PREFIX + "system.reg")
            base_entries[prefixes.PREFIX + "system.reg"] = b"old registry"
            base_entries["home/username/snake.exe"] = b"snake payload"
            for dll in ("d3d9.dll", "d3d11.dll", "dxgi.dll"):
                base_entries["home/username/" + dll] = (dll + " payload").encode()
            with zipfile.ZipFile(base_zip, "w") as archive:
                for name, data in base_entries.items():
                    archive.writestr(name, data)
            for name, data in valid_entries("win64").items():
                if name.startswith(prefixes.PREFIX) and name.endswith(".reg"):
                    if name.endswith("user.reg"):
                        data = without_audio_setting(data)
                    (state / name[len(prefixes.PREFIX):]).write_bytes(data)
            (state / "drive_c/windows/inf").mkdir(parents=True)
            for name in ("winebus.inf", "winehid.inf", "wineusb.inf", "winexinput.inf"):
                (state / "drive_c/windows/inf" / name).write_text("inf\n")
            (state / "drive_c/windows/inf/winehid.inf").write_text("booted HID INF\n")

            prefixes.package_boot_state(base_zip, state, out_zip)
            prefixes.validate_prefix_zip(out_zip, "win64")
            with zipfile.ZipFile(out_zip) as archive:
                self.assertEqual(archive.read("home/username/snake.exe"), b"snake payload")
                for dll in ("d3d9.dll", "d3d11.dll", "dxgi.dll"):
                    self.assertEqual(archive.read("home/username/" + dll), (dll + " payload").encode())
                self.assertEqual(archive.read(prefixes.PREFIX + ".update-timestamp"), b"disable\n")
                self.assertEqual(archive.read(prefixes.PREFIX + "drive_c/windows/inf/winehid.inf"), b"booted HID INF\n")
                self.assertIn(b'"Audio"="alsa"', archive.read(prefixes.PREFIX + "user.reg"))
                system = archive.read(prefixes.PREFIX + "system.reg")
                self.assertNotIn(b'"winemenubuilder"=', system)
                self.assertIn(b"RpcSs", system)
                self.assertIn(b"PlugPlay", system)

    def test_registry_fragment_merge_is_complete_and_idempotent(self):
        registry = 'WINE REGISTRY Version 2\n#arch=win64\n\n[Software\\Wine] 1\n'
        fragment = ('; comment\n[Software\\Microsoft\\Windows NT\\CurrentVersion] 1\n'
                    '"ProductName"="Microsoft Windows 7"\n\n'
                    '[Software\\Microsoft\\.NETFramework] 1\n"InstallRoot"="C:\\windows"\n')
        merged, added = prefixes.merge_registry_fragment(registry, fragment)
        self.assertEqual(len(added), 2)
        self.assertIn('"ProductName"="Microsoft Windows 7"', merged)
        self.assertIn('[Software\\Microsoft\\.NETFramework] 1', merged)
        again, added_again = prefixes.merge_registry_fragment(merged, fragment)
        self.assertEqual(added_again, [])
        self.assertEqual(again, merged)

    def test_guard_requires_completed_marker_and_enabled_services(self):
        entries = valid_entries("win32")
        prefixes._validate_prefix(entries, "win32")
        entries.pop(prefixes.PREFIX + ".update-timestamp")
        with self.assertRaisesRegex(ValueError, "completed Wine update marker"):
            prefixes._validate_prefix(entries, "win32")

        entries = valid_entries("win32")
        system = entries[prefixes.PREFIX + "system.reg"].replace(
            b'"Start"=dword:00000003', b'"Start"=dword:00000004', 1)
        entries[prefixes.PREFIX + "system.reg"] = system
        with self.assertRaisesRegex(ValueError, "service RpcSs is disabled"):
            prefixes._validate_prefix(entries, "win32")

    def test_win64_runtime_defaults_are_allowed_when_prefix_has_no_service_overrides(self):
        entries = valid_entries("win64")
        system_key = prefixes.PREFIX + "system.reg"
        text = entries[system_key].decode()
        for name in (*prefixes.SERVICES,):
            section = f"[System\\\\CurrentControlSet\\\\Services\\\\{name}] 1\n\"Start\"=dword:00000003\n\n"
            if name == "PlugPlay":
                section = f"[System\\\\CurrentControlSet\\\\Services\\\\{name}] 1\n\"Start\"=dword:00000002\n\n"
            text = text.replace(section, "")
        for key in prefixes.CONTROLLER_KEYS[2:]:
            text = "\n".join(line for line in text.splitlines() if not line.startswith("[" + key + "]")) + "\n"
        entries[system_key] = text.encode()
        prefixes._validate_prefix(entries, "win64")
        with self.assertRaisesRegex(ValueError, "missing service registration RpcSs"):
            prefixes._validate_prefix(entries, "win64", require_full_init=True)

    def test_guard_checks_both_architecture_hives_and_hid_state(self):
        prefixes._validate_prefix(valid_entries("win32"), "win32")
        prefixes._validate_prefix(valid_entries("win64"), "win64")
        entries = valid_entries("win64")
        del entries[prefixes.PREFIX + "system.reg"]
        with self.assertRaisesRegex(ValueError, "missing home/username/.wine/system.reg"):
            prefixes._validate_prefix(entries, "win64")

        entries = valid_entries("win64")
        entries[prefixes.PREFIX + "user.reg"] = without_audio_setting(entries[prefixes.PREFIX + "user.reg"])
        prefixes._validate_prefix(entries, "win64")

    def test_linked_wine_runtime_keeps_services_hid_and_d3d_modules(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            prefix = temp / "prefix.zip"
            runtime = temp / "wine.zip"
            linked = "usr/lib/x86_64-linux-gnu/wine/x86_64-windows/"
            entries = {prefixes.PREFIX + "drive_c/windows/system32.link": ("/" + linked).encode()}
            with zipfile.ZipFile(prefix, "w") as archive:
                for name, data in entries.items():
                    archive.writestr(name, data)
            modules = ("winealsa.drv", "services.exe", "rpcss.exe", "plugplay.exe", "winedevice.exe",
                       "winebus.sys", "winehid.sys", "wineusb.sys", "d3d11.dll", "d3d12.dll",
                       "d3d12core.dll", "msmpeg2vdec.dll")
            unix = linked.replace("x86_64-windows/", "x86_64-unix/")
            with zipfile.ZipFile(runtime, "w") as archive:
                for name in modules:
                    archive.writestr(linked + name, b"wine module")
                archive.writestr(unix + "winebus.so", b"winebus")
                archive.writestr(unix + "wineusb.so", b"wineusb")
                for plugin in ("libav", "playback", "typefindfunctions", "avi", "isomp4",
                               "matroska", "ogg", "wavparse", "flac", "vorbis"):
                    archive.writestr("usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgst"
                                     + plugin + ".so", b"gst plugin")
            prefixes.validate_service_runtime(prefix, runtime)
            incomplete = temp / "incomplete.zip"
            with zipfile.ZipFile(incomplete, "w") as archive:
                for name in modules:
                    if name != "d3d12.dll":
                        archive.writestr(linked + name, b"wine module")
                archive.writestr(unix + "winebus.so", b"winebus")
                archive.writestr(unix + "wineusb.so", b"wineusb")
            with self.assertRaisesRegex(ValueError, "d3d12.dll"):
                prefixes.validate_service_runtime(prefix, incomplete)

            without_alsa = temp / "without-alsa.zip"
            with zipfile.ZipFile(without_alsa, "w") as archive:
                for name in modules:
                    if name != "winealsa.drv":
                        archive.writestr(linked + name, b"wine module")
                archive.writestr(unix + "winebus.so", b"winebus")
                archive.writestr(unix + "wineusb.so", b"wineusb")
            with self.assertRaisesRegex(ValueError, "winealsa.drv"):
                prefixes.validate_service_runtime(prefix, without_alsa)


if __name__ == "__main__":
    unittest.main()
