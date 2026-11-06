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
| msvcr100.dll | c8e9bbb1aadcda1584fe6fa1c7371ab1d79d7f7ea01c6a9da18ee9086fb863e6 |
| msvcr110.dll | fcbaef875c3201f903adf82f01d9ad956f40eeaec1cf04bc6b4c97f532265cff |
| msvcr120.dll | 5aa48ebd548dd5d985856383efc8ecc62b2cd6b75e0aa325de145d39535bd694 |
| msvcr70.dll | 0406d46a99a8926f1af0a1a9f06c97a6bd8790bd8394f6ed5c0ddd3a4c1ec266 |
| msvcr71.dll | 641c1554fbea10f82eff4bc06f520c9e6bfea744a41d2cf6ab88c77c08363b69 |
| msvcr80.dll | 13b75e2323112b54d741eee2f7565c505f63db60cc1e75f519b49acb6d8776cc |
| msvcr90.dll | 7635463213c7588672b94915b9bb3cb37a6a16d922bfb93144d1ecf3b4f615c2 |
| msvcrt.dll | 03c832d2d826fc334dec79714295219c095cf91261263a46a98d134220a18b6b |
| ucrtbase.dll | dda971fcfdea0abdc94aab827360278824d0b9f5c0eb1ef5048f2c27236cf5f8 |
