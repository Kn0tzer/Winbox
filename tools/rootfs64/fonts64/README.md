# DejaVu faces for the 64-bit glibc rootfs

`glibc-rootfs64.zip` historically carried no font faces at all, so the
64-bit guest's font enumeration came back empty of real text fonts:
fontconfig's configured directories (`/usr/share/fonts` above all)
held nothing, and the only faces anywhere in the 64-bit runtime set
were Wine's own bundled bitmap-era fonts. Wine Gecko aborts startup
with "unable to find a usable font (serif)" in that state (FD-65);
any GDI/DirectWrite consumer on the 64-bit stack faces the same
empty list. These six faces give the 64-bit rootfs the same DejaVu
coverage the 32-bit rootfs already ships (Sans + Bold + Mono faces,
staged there by `tools/rootfs32/stage-fonts.py` and
`stage-fonts32.sh`), plus the Serif pair Gecko's startup requires.
`stage-fonts64.sh` merges them into both `glibc-rootfs64.zip` copies
under `usr/share/fonts/truetype/dejavu/`, next to the package
copyright at `usr/share/doc/fonts-dejavu-core/copyright`.

Source: Debian/Ubuntu package `fonts-dejavu-core` 2.37-8
(`https://dejavu-fonts.github.io/`). License: Bitstream Vera license /
public-domain DejaVu changes — freely redistributable (full text in
`copyright` in this directory, staged into the rootfs alongside the
faces). The three faces shared with `fonts32/` are byte-identical to
the copies pinned there; `DejaVuSans.ttf` is byte-identical to the
face `tools/rootfs32/stage-fonts.py` stages into the 32-bit rootfs.

| file | bytes | sha256 |
| --- | --- | --- |
| DejaVuSans.ttf | 759720 | ae7b7855e115a5966d8b1b3f80f254ccc117ec86f9965e202ee2940453837280 |
| DejaVuSans-Bold.ttf | 708920 | 5c1247acef7f2b8522a31742c76d6adcb5569bacc0be7ceaa4dc39dd252ce895 |
| DejaVuSansMono.ttf | 343140 | c805f9436dbc268644c1d9584f01a601a653e028e08fd74b9b949f6cf8304d88 |
| DejaVuSansMono-Bold.ttf | 334268 | 3a3c502eeff669a231549e80df9f7c49de109bafe303170409e905d0b31a38fe |
| DejaVuSerif.ttf | 380660 | 8f2c103bfa3fd5de71f1b92b18f21906b5a26871fb7e19a9a4c9af539c3cc7ab |
| DejaVuSerif-Bold.ttf | 356668 | 847b33e13925f19ff87e4d934d6b3cf7cac35ce16424f6f670e40c2f377cf2df |
