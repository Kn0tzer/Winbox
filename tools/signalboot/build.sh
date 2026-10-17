#!/bin/bash
# Build CRT-free signalboot.exe (avoids i686-w64-mingw32 CRT constructor-table bug)
set -e
cd "$(dirname "$0")"
i686-w64-mingw32-gcc -nostdlib -e __start -o signalboot.exe signalboot.c -lkernel32 -lntdll
echo "Built: $(ls -la signalboot.exe)"
