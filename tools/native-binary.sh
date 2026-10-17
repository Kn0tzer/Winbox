#!/usr/bin/env bash
# Source this helper to locate the native diagnostic build consistently.
bw64_native_binary() {
    local repo requested candidate
    repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    requested="${1:-${BW64_BIN:-}}"
    if [[ -n "$requested" ]]; then
        [[ -x "$requested" ]] || { echo "error: native binary not executable: $requested" >&2; return 1; }
        printf '%s\n' "$requested"
        return
    fi
    for candidate in "$repo/project/linux/Build/X64Headless/boxedwine" "$repo/project/linux/Build/Release/boxedwine"; do
        if [[ -x "$candidate" ]]; then printf '%s\n' "$candidate"; return; fi
    done
    echo "error: build with make -C $repo/project/linux x64, or set BW64_BIN" >&2
    return 1
}
