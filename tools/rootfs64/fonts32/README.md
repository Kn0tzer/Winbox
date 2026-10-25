# DejaVu Bold/Mono faces for the 32-bit glibc rootfs

`glibc-rootfs32.zip` historically carried exactly one font face
(`DejaVuSans.ttf`, staged by `tools/rootfs32/stage-fonts.py`), so bold and
monospace requests on the 32-bit guest synthesized or fell back to the
regular face. These three faces close that gap; `stage-fonts32.sh` merges
them into both `glibc-rootfs32.zip` copies under
`usr/share/fonts/truetype/dejavu/`, next to the existing regular face.

Source: Debian/Ubuntu package `fonts-dejavu-core` 2.37-8
(`https://dejavu-fonts.github.io/`). License: Bitstream Vera license /
public-domain DejaVu changes — freely redistributable (full text in the
package's `usr/share/doc/fonts-dejavu-core/copyright`, already staged in
the rootfs by `stage-fonts.py`).

| file | bytes | sha256 |
| --- | --- | --- |
| DejaVuSans-Bold.ttf | 708920 | 5c1247acef7f2b8522a31742c76d6adcb5569bacc0be7ceaa4dc39dd252ce895 |
| DejaVuSansMono.ttf | 343140 | c805f9436dbc268644c1d9584f01a601a653e028e08fd74b9b949f6cf8304d88 |
| DejaVuSansMono-Bold.ttf | 334268 | 3a3c502eeff669a231549e80df9f7c49de109bafe303170409e905d0b31a38fe |
