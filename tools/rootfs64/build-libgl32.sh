#!/bin/bash
# Build the 32-bit guest GL shim from the shared source. Keep its functions and
# capability declarations identical to libgl64; only the syscall ABI differs.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HERE/libgl32"
python3 - "$HERE" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
s = (root / 'libgl64/libgl64.c').read_text()
start = s.index('static inline uint64_t gl64_trap(')
end = s.index('\n}', start) + 2
s = s[:start] + '''static inline uint64_t gl64_trap(uint64_t fnId, GL64Args* args) {
    uint32_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"((uint32_t)GL64_SYSCALL_NR), "b"((uint32_t)fnId),
          "c"((uint32_t)(uintptr_t)args)
        : "memory");
    return ret;
}''' + s[end:]
(root / 'libgl32/libgl32.c').write_text(s)
PY
"${GL32_CC:-i686-linux-gnu-gcc}" -shared -fPIC -O2 -fvisibility=hidden \
    -nostdlib -ffreestanding -Wl,-soname,libGL.so.1 \
    -I "$HERE/libgl64" -o "$HERE/libgl32/libGL.so.1" "$HERE/libgl32/libgl32.c"
echo "Built $HERE/libgl32/libGL.so.1"
