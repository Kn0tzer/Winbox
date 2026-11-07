# wine10-fd169r-i386 — FD-169 remainder rebuilds (i386)

Two Microsoft C++ runtime DLLs rebuilt from the pinned Wine 10.0
sources with the FD-169 remainder implementations (see
`tools/buildWine/patches/wine10-fd169r-crt2.patch` and
`docs/reports/fd169-remainder-20261107.md`). msvcp140_2.dll gains
the complete C++17 special-math set (__std_smf_*), hand-written
and validated against a 50-digit mpmath reference grid;
msvcp140_atomic_wait.dll gains the execution wait/wake pair, the
shared-mutex pair, the CRT allocation pair and the tzdb deleters,
backported from upstream Wine (post-10.0). They replace the
WineHQ wine-stable 10.0 copies inside wine32.zip via the
substitution step in `tools/rootfs32/build-wine32-base.sh`, which
verifies each file against the sha256 pins below before use
(fail-closed). Regenerate with
`tools/rootfs32/build-wine10-fd169r.sh`.

| file | sha256 |
|---|---|
| msvcp140_2.dll | 2c8c70331d04ea0a4f3826d7719e5e77f6cd65c174b6c2e6017a729fa3e082ad |
| msvcp140_atomic_wait.dll | c12bc9e47019340c1777bf4f5a4e0ea1b1d1906342209db84128d190fd3a1348 |
