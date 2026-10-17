#!/bin/bash
# Build the cpu-diff harness.
#
# Two binaries:
#  1. build/cpu-diff (native, ARM64 on this box): the CPU64 emulator core
#     (cpu64.cpp, kmemory64.cpp, fpu.cpp, ripSampler.cpp, bstring.cpp)
#     compiled with -DBOXEDWINE_GUEST_X64=1, plus harness.cpp (orchestration,
#     differ, reporting). The harness spawns qemu-x86_64 for native runs.
#  2. runner/runner.elf (x86-64, static): the native snippet runner executed
#     under qemu-x86_64. Built with x86_64-linux-gnu-g++.
#
# Test-only scaffolding: harness.cpp stubs a few symbols the differential
# snippets can never reach (ksyscall64, raiseSyncFault, ...). No emulator
# behavior is modified.
set -e
cd "$(dirname "$0")"
REPO="$(cd ../.. && pwd)"
INC="-I$REPO/include -I$REPO/lib/simde"
DEF="-DBOXEDWINE_GUEST_X64=1"
FLAGS="-std=c++20 -O2 $DEF -Wall"
mkdir -p build
SRCS="$REPO/source/emulation/cpu/cpu64.cpp \
      $REPO/source/kernel/kmemory64.cpp \
      $REPO/source/emulation/cpu/common/fpu.cpp \
      $REPO/source/emulation/cpu/normal/ripSampler.cpp \
      $REPO/source/util/bstring.cpp \
      harness.cpp"
for s in $SRCS; do
  o="build/$(echo "$s" | tr '/.' '__').o"
  echo "CC $s"
  g++ $FLAGS $INC -c "$s" -o "$o"
done
echo "CC softfloat (lib/softfloat/source/*.c + 8086-SSE/*.c)"
SOFTINC="-I$REPO/lib/softfloat/source/include -I$REPO/lib/softfloat/source -I$REPO/lib/softfloat/source/8086-SSE"
mkdir -p build/softfloat
for s in $REPO/lib/softfloat/source/*.c $REPO/lib/softfloat/source/8086-SSE/*.c; do
  o="build/softfloat/$(basename $s .c).o"
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ build.sh -nt "$o" ] ||
     [ -n "$(find "$REPO/lib/softfloat/source" -type f -name '*.h' -newer "$o" -print -quit)" ]; then
    gcc -O2 $SOFTINC -c "$s" -o "$o"
  fi
done
echo "LD build/cpu-diff"
g++ build/*.o build/softfloat/*.o -o build/cpu-diff
echo "CC runner/runner.cpp (x86-64)"
x86_64-linux-gnu-g++ -static -O2 runner/runner.cpp -o runner/runner.elf
echo "OK: build/cpu-diff + runner/runner.elf"
