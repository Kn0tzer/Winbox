# libudev1-x86_64 — libudev for the 64-bit runtime (FD-195)

`libudev.so.1` is the genuine Debian bookworm binary from
`libudev1_252.39-1~deb12u2_amd64.deb`
(sha256 6f7e08f11c4809d2e5998c775f7b423ab5ec2ce0309494b4a2615037052245e2),
stored under its SONAME as a regular file — the same convention the
32-bit runtime uses for its i386 libudev (also systemd 252.39).
It depends only on libc.so.6 and the dynamic loader.

## Why it ships

winebus's udev backend dlopens libudev.so.1 to enumerate evdev
devices. The 64-bit runtime never carried the library (the bookworm
`wine64` image closure does not include libudev1), so on the 64-bit
side the backend stayed dead: winebus created only its two USB HID
devices and the game controller the host feed had opened never
became a guest device — guest programs saw no controller at all
(XInput slot 0 never connected, DirectInput enumerated 0 devices),
while the 32-bit side, whose runtime does ship libudev, worked.
`tools/rootfs64/stage-libudev64.sh` merges this file into the
shipped wine64.zip archives as
`usr/lib/x86_64-linux-gnu/libudev.so.1`; the canonical image recipe
in `tools/rootfs64/build-wine64-zip.sh` now also installs libudev1
so a full rebuild carries it natively.

## Packaging integrity

sha256 of this exact file, verified by
`tools/rootfs64/check_runtime_completeness.py` (source hash + the
shipped `wine64.zip` member hash):

| file | bytes | sha256 |
| --- | --- | --- |
| libudev.so.1 | 182544 | 99a5e38f8b45ec2729e5bc24d2d8e2f04d260a2455431a1f9d61f500b31060dd |
