#!/usr/bin/env python3
"""
vkgl_spec_census.py — Vulkan / OpenGL specification-coverage census.

Enumerates, mechanically and reproducibly, the sets whose intersection is
the project's graphics-API test coverage, so the coverage share is a
number a script produces rather than an estimate a person repeats.

Definitions (stated once, used everywhere):

Vulkan
  SPEC       every command name in the Khronos registry (vk.xml, the copy
             vendored with the DXVK headers the coverage harness builds
             against): <proto> names plus alias names, each counted once.
  EXPOSED    names in the guest ICD shim dispatch tables
             (tools/rootfs64/libvk64/libvk64.c, libvk32 twin): E() bridged,
             S() shim-local, TAIL() loud stub. What a guest can obtain.
  BRIDGE     names with a handler case in the host bridge
             (source/vulkan/vk64bridge.cpp, both guest ABIs in one file).
  EXERCISED  names the coverage harness (tools/vkcov/vkcov.c) actually
             calls through its timed test macros, not merely loads.

OpenGL
  SPEC       every entry point in the Khronos-derived table the project
             carries (tools/opengl/glfunctions.h — the glshim universe,
             core + every vendor extension; stated as the denominator
             with that caveat).
  BRIDGE     names with a handler case in the host bridge
             (source/opengl/gl64bridge.cpp).
  SHIM-REAL  names the guest libGL implements for real
             (tools/rootfs64/libgl64/libgl64.c).
  SHIM-STUB  names the guest libGL exports as silent no-ops
             (libgl64_stubs.h GLSTUB lines) so Wine's dlsym loop succeeds.
             A harness "working" verdict on one of these is vacuous.
  EXERCISED  names the coverage harness (tools/glcov/glcov.c) references
             (its resolve list and call sites, intersected with SPEC).

Usage: vkgl_spec_census.py [repo-root] [--json-out PATH]
Prints a summary; with --json-out also writes the full set algebra,
including the actionable difference lists, as JSON.
"""
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def vk_spec(root: Path):
    xml = root / "tools/dxvk/src/include/vulkan/registry/vk.xml"
    tree = ET.parse(xml)
    names = set()
    for cmd in tree.getroot().iter("command"):
        if cmd.get("alias"):
            names.add(cmd.get("name"))
        else:
            n = cmd.find("./proto/name")
            if n is not None and n.text:
                names.add(n.text)
    return names


def bridge_cases(path: Path, prefix: str):
    txt = path.read_text(errors="replace")
    return set(re.findall(r"case " + prefix + r"([A-Za-z0-9_]+)\s*:", txt))


def shim_table(path: Path):
    # Entries are E(name, VK64_fn_name), S(name) or TAIL(name): the name is
    # followed by a comma (two-arg form) or a close paren (one-arg form).
    txt = path.read_text(errors="replace")
    out = {}
    for kind, name in re.findall(r"\b(E|S|TAIL)\((vk[A-Za-z0-9_]+)[,\)]", txt):
        out[name] = kind
    return out


def vk_exercised(path: Path):
    txt = path.read_text(errors="replace")
    loaded = set(re.findall(r"LOADFN\((vk[A-Za-z0-9_]+)\)", txt))
    called = set(re.findall(r"\bT[12]V?\((vk[A-Za-z0-9_]+)\s*,", txt))
    called |= set(re.findall(r'rep\("(vk[A-Za-z0-9_]+)"', txt))
    # the extended-coverage section reports through REP_GPA_V / REP_GPA_R
    # wrappers whose first argument is the handler name
    called |= set(re.findall(r"\bREP_GPA_[VR]\((vk[A-Za-z0-9_]+)\s*,", txt))
    return loaded, called


def gl_spec(root: Path):
    txt = (root / "tools/opengl/glfunctions.h").read_text(errors="replace")
    return set(re.findall(r"APIENTRY\s+(gl[A-Za-z0-9_]+)\s*\(", txt))


def gl_shim(path: Path, stubs: Path):
    txt = path.read_text(errors="replace")
    real = set(re.findall(r"^API\s+[^;\n]*?\b(gl[A-Za-z0-9_]+)\s*\(", txt, re.M))
    stxt = stubs.read_text(errors="replace")
    stub = set(re.findall(r"GLSTUB\((gl[A-Za-z0-9_]+)\)", stxt))
    return real, stub


def gl_exercised(path: Path):
    # The harness reports through rep("name", ...) literals and the
    # GENDEL(gen, del) macro; its resolve list is LOADGL(name). Raw name
    # sets: core GL entry points are not in the extension table, so no
    # intersection with SPEC is applied here.
    txt = path.read_text(errors="replace")
    reported = set(re.findall(r'rep\("(gl[A-Za-z0-9_]+)"', txt))
    for gen, dele in re.findall(r"GENDEL\((gl[A-Za-z0-9_]+)\s*,\s*(gl[A-Za-z0-9_]+)\)", txt):
        reported.add(gen)
        reported.add(dele)
    reported |= set(re.findall(r"\bT[12]V?\((gl[A-Za-z0-9_]+)\s*,", txt))
    resolved = set(re.findall(r"LOADGL\((gl[A-Za-z0-9_]+)\)", txt))
    return reported, resolved


def abi_enum(path: Path, prefix: str):
    txt = path.read_text(errors="replace")
    return set(re.findall(prefix + r"([A-Za-z0-9_]+)", txt))


def pct(a, b):
    return f"{(100.0 * a / b):.1f}%" if b else "n/a"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    root = Path(args[0]) if args else Path.cwd()
    json_out = None
    if "--json-out" in sys.argv:
        json_out = sys.argv[sys.argv.index("--json-out") + 1]

    # ---------------- Vulkan ----------------
    spec = vk_spec(root)
    shim64 = shim_table(root / "tools/rootfs64/libvk64/libvk64.c")
    shim32 = shim_table(root / "tools/rootfs64/libvk32/libvk32.c")
    exposed = set(shim64) | set(shim32)
    bridge = bridge_cases(root / "source/vulkan/vk64bridge.cpp", "VK64_fn_")
    abi64 = abi_enum(root / "source/vulkan/vk64bridge_abi.h", "VK64_fn_")
    abi32 = abi64  # vk32bridge_abi.h shares the 64-bit enum by design
    loaded, called = vk_exercised(root / "tools/vkcov/vkcov.c")

    vk = {
        "spec": len(spec),
        "exposed_shim_union": len(exposed),
        "exposed_shim64": len(shim64),
        "exposed_shim32": len(shim32),
        "abi64_ids": len(abi64),
        "abi32_ids": len(abi32),
        "bridge_cases": len(bridge),
        "harness_loaded": len(loaded),
        "harness_called": len(called),
        "called_share_of_spec": pct(len(called & spec), len(spec)),
        "exposed_not_called": sorted(exposed - called),
        "bridge_not_called": sorted(bridge - called),
        "spec_not_exposed": sorted(spec - exposed),
        "called_not_in_spec": sorted(called - spec),
        "bridge_internal_names": sorted(
            n for n in bridge - called
            if n == "vkCmdSetExtendedDynamicState"),
        "tail_in_shim_not_called": sorted(
            n for n, k in {**shim32, **shim64}.items()
            if k == "TAIL" and n not in called
        ),
    }

    # ---------------- OpenGL ----------------
    gspec = gl_spec(root)
    gbridge = bridge_cases(root / "source/opengl/gl64bridge.cpp", "GL64_fn_")
    gabi = abi_enum(root / "source/opengl/gl64bridge_abi.h", "GL64_fn_")
    # GL64_fn_traceProc is the bridge's wglGetProcAddress plumbing
    # pseudo-function, not a GL entry point: excluded from both sets.
    gbridge.discard("traceProc")
    gabi.discard("traceProc")
    greal, gstub = gl_shim(
        root / "tools/rootfs64/libgl64/libgl64.c",
        root / "tools/rootfs64/libgl64/libgl64_stubs.h",
    )
    gcalled, gresolved = gl_exercised(root / "tools/glcov/glcov.c")

    gl = {
        "spec_glfunctions_table": len(gspec),
        "abi_ids": len(gabi),
        "bridge_cases": len(gbridge),
        "abi_not_cased": sorted(gabi - gbridge),
        "shim_real": len(greal),
        "shim_stub_silent": len(gstub),
        "harness_resolved": len(gresolved),
        "harness_exercised": len(gcalled),
        "exercised_share_of_spec": pct(len(gcalled & gspec), len(gspec)),
        "exercised_share_of_bridge": pct(len(gcalled & gbridge), len(gbridge)),
        "exercised_share_of_abi": pct(len(gcalled & gabi), len(gabi)),
        "bridge_not_exercised": sorted(gbridge - gcalled),
        "abi_not_exercised": sorted(gabi - gcalled),
        "exercised_but_silent_stub": sorted(gcalled & gstub),
        "exercised_not_in_bridge": sorted(gcalled - gbridge),
    }

    print("=== Vulkan ===")
    for k in ("spec", "exposed_shim_union", "exposed_shim64", "exposed_shim32",
              "abi64_ids", "abi32_ids", "bridge_cases", "harness_loaded",
              "harness_called", "called_share_of_spec"):
        print(f"  {k}: {vk[k]}")
    print(f"  exposed-not-called: {len(vk['exposed_not_called'])}")
    print(f"  bridge-not-called: {len(vk['bridge_not_called'])}")
    print(f"  spec-not-exposed: {len(vk['spec_not_exposed'])}")
    print(f"  TAIL-in-shim-not-called: {len(vk['tail_in_shim_not_called'])}")
    print(f"  bridge-internal (multiplexed trap, no own entry point): "
          f"{vk['bridge_internal_names']}")
    print("=== OpenGL ===")
    for k in ("spec_glfunctions_table", "abi_ids", "bridge_cases", "shim_real",
              "shim_stub_silent", "harness_resolved", "harness_exercised",
              "exercised_share_of_spec", "exercised_share_of_bridge",
              "exercised_share_of_abi"):
        print(f"  {k}: {gl[k]}")
    print(f"  abi-not-cased: {gl['abi_not_cased']}")
    print(f"  bridge-not-exercised: {len(gl['bridge_not_exercised'])}")
    print(f"  exercised-but-silent-stub (vacuous rows): "
          f"{len(gl['exercised_but_silent_stub'])}")

    if json_out:
        Path(json_out).write_text(json.dumps({"vulkan": vk, "opengl": gl},
                                             indent=1))
        print(f"wrote {json_out}")


if __name__ == "__main__":
    main()
