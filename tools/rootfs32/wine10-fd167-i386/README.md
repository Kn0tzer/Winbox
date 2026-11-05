# Wine 10.0 FD-167 contract backports (i386)

`build-wine10-fd167.sh` builds the seven Wine 10.0 DLLs whose FD-167
demo-reachable exports were spec stubs in the shipped wine32.zip, with
`tools/buildWine/patches/wine10-fd167-contracts.patch` applied to the
same pinned wine-10.0 tarball the win32u backport uses:

- bluetoothapis — the Bluetooth radio/GATT entries gain their
  documented no-radio behaviour (invalid-handle errors, never a fake
  success); bthprops.cpl forwards to them.
- winusb — the eight WinUSB entries gain their documented no-device
  behaviour (FALSE + ERROR_INVALID_HANDLE).
- mf — MFCreateDeviceSource implemented (delegates to the activation
  object, E_NOTIMPL without one — upstream behaviour).
- mfplat — MFCreateMediaBufferWrapper implemented as a genuine
  offset/length window over an existing IMFMediaBuffer.
- propsys — VariantCompare implemented on PropVariantCompareEx plus
  the VT_BOOL/VT_DATE/VT_FILETIME cases that function leaves open.
- wevtapi — EvtRender reports ERROR_CALL_NOT_IMPLEMENTED instead of
  killing the caller (upstream behaviour).

Outputs land in this directory (`<dll>.dll` per DLL) together with
their sha256 pins. `tools/rootfs32/build-wine32-base.sh` substitutes
them into the rebuilt wine32.zip fail-closed against those pins, the
same way it substitutes the win32u backport. The census that found the
stubs is FD-167's record (tools/conformance/dll_export_baseline.json).
