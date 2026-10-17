#!/bin/bash
# Single-exe D3D smoke runner. Usage: run-single.sh <name>
# where <name> selects a hardcoded config below.
cd /home/ubuntu/boxedwine64 || exit 1
case "$1" in
  d10bare)
    export TAG="d10bare1" EXE_PATH="tools/d3dtests/d10bare.exe" EXE_NAME="d10bare.exe"
    export DONE_RE="d10bare: DONE"
    export ASSETS='[["d3d10core.dll","tools/dxvk/build.w64/src/d3d10/d3d10core.dll"],["d3d11.dll","tools/dxvk/build.w64/src/d3d11/d3d11.dll"],["dxgi.dll","tools/dxvk/build.w64/src/dxgi/dxgi.dll"],["libvulkan.so.1","tools/rootfs64/libvk64/libvulkan.so.1"]]'
    export SPAWN_ENV="BW64_VKTRACE=2
BW64_VKFRAME=1
WINEDLLOVERRIDES=d3d10core=n;d3d11=n;dxgi=n
LD_LIBRARY_PATH=/home/username"
    ;;
  d11multi)
    export TAG="d11multi1" EXE_PATH="tools/dxvk/probe/d11multi.exe" EXE_NAME="d11multi.exe"
    export DONE_RE="d11multi: DONE"
    export ASSETS='[["d3d11.dll","tools/dxvk/build.w64/src/d3d11/d3d11.dll"],["dxgi.dll","tools/dxvk/build.w64/src/dxgi/dxgi.dll"],["libvulkan.so.1","tools/rootfs64/libvk64/libvulkan.so.1"]]'
    export SPAWN_ENV="BW64_VKTRACE=2
BW64_VKFRAME=1
WINEDLLOVERRIDES=d3d11=n;dxgi=n
LD_LIBRARY_PATH=/home/username"
    ;;
  d11swap)
    export TAG="d11swap1" EXE_PATH="tools/dxvk/probe/d11swap.exe" EXE_NAME="d11swap.exe"
    export DONE_RE="d11swap: DONE"
    export ASSETS='[["d3d11.dll","tools/dxvk/build.w64/src/d3d11/d3d11.dll"],["dxgi.dll","tools/dxvk/build.w64/src/dxgi/dxgi.dll"],["libvulkan.so.1","tools/rootfs64/libvk64/libvulkan.so.1"]]'
    export SPAWN_ENV="BW64_VKTRACE=2
BW64_VKFRAME=1
WINEDLLOVERRIDES=d3d11=n;dxgi=n
LD_LIBRARY_PATH=/home/username"
    ;;
  dconf11b)
    export TAG="dconf11b2" EXE_PATH="tools/dxvk/probe/d3dconf11b.exe" EXE_NAME="d3dconf11b.exe"
    export DONE_RE="d3dconf11b: DONE"
    export ASSETS='[["d3d11.dll","tools/dxvk/build.w64/src/d3d11/d3d11.dll"],["dxgi.dll","tools/dxvk/build.w64/src/dxgi/dxgi.dll"],["libvulkan.so.1","tools/rootfs64/libvk64/libvulkan.so.1"]]'
    export SPAWN_ENV="BW64_VKTRACE=2
BW64_VKFRAME=1
WINEDLLOVERRIDES=d3d11=n;dxgi=n
LD_LIBRARY_PATH=/home/username"
    ;;
  *)
    echo "unknown config $1" >&2; exit 1 ;;
esac
export TIMEOUT=420000
exec node web/tests/d3d-single.mjs
