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
| msvcr100.dll | f15f7a9b6eb6ce7e0fa921a3343f91cac34f808ee443eda609630ce0f81dab9d |
| msvcr110.dll | 2b5bff73c1e77843723e3335c106a53b9ae4119839aceb16d610ebe61d7299c5 |
| msvcr120.dll | 0e4ba5e74c22db4de9a8889d89d247b708e8ba657029b75b5f7dd1f9dbb58b92 |
| msvcr70.dll | 1d5c6c16b3d095039142037a92256cd660f00fa29ecaddd07ef0722b2130f6b7 |
| msvcr71.dll | 60ac37db454b821d6245fce39830e37126a065f0386f836d98d4e89bf9565b41 |
| msvcr80.dll | 7aa234283cd4c0dd378540184db724360a189d47013ae4259d4622c2f19ecd7c |
| msvcr90.dll | c3f3062bb3ba6e5084da24ad66dd5dec18cc3633e0fb0b166d54a0d5a827dada |
| msvcrt.dll | f3cebbf0855200a772733b1d0aa431fdd77cea78b1cb03b6427311921ca98cec |
| ucrtbase.dll | 7c1782913e4c865252348fd38cbb402bc3d8dd6bd6bf088501d4ffbaa3c5264c |
