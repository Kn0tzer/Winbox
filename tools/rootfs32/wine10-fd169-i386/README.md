# wine10-fd169-i386 — FD-169 CRT rebuilds (i386)

Nine Microsoft C-runtime DLLs rebuilt from the pinned Wine 10.0
sources with the FD-169 implementations (see
`tools/buildWine/patches/wine10-fd169-crt.patch` and
`docs/reports/fd169-crt-family-20261105.md`). They replace the
WineHQ wine-stable 10.0 copies inside wine32.zip via the
substitution step in `tools/rootfs32/build-wine32-base.sh`, which
verifies each file against the sha256 pins below before use
(fail-closed). Regenerate with
`tools/rootfs32/build-wine10-fd169.sh`.

| file | sha256 |
|---|---|
| msvcr100.dll | 29007c22d0081fe2f499a8eb0d1341449bdd58c3d47a8c5e197b85910a0b3322 |
| msvcr110.dll | e2e0a731b4b92de404420f00c88cde465a5303f51e2ce106016911fff70d5b8f |
| msvcr120.dll | 0819776bd30bf6cb2e2e041af96f3df82c94410049d582decf7a2c6ef90a1dbf |
| msvcr70.dll | cad2f54a8fb5654f5bc42290e2d6ed21789e59e7ccd9bca4c1e0c5947c2ef6a9 |
| msvcr71.dll | 05e32b5f829b5d1bbdc9f4e028d3fc52d3ae27cdecffb4e519ce7d3decfc2af8 |
| msvcr80.dll | f4cbb861805987dee5b5d27612298510ba6ffe6c95dc9bf93c06a5a8a7bdcc82 |
| msvcr90.dll | 501ceebce97c19dc2bf47e6b6a82ccf2fbd00f2644ffd8f21917f40c8741213c |
| msvcrt.dll | 6bb88c48aeba9b92b12dd9b0c61554996758a5b00f94c7aca561bdc2af0e5a72 |
| ucrtbase.dll | fa99ad18033d40d26d567749e25cd8e60c6986ed2c14fdeb07c59ac4ad0b1c17 |
