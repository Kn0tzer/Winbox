#!/usr/bin/env python3
"""pe_census.py - static import-resolution census for Winbox programs.

For every program under test (PE executables + the DLLs bundled with them),
parse the PE import tables (regular + delay-load, reported separately) and
resolve every imported (DLL, function) pair against the export tables of
every PE the runtime actually stages, plus the DLLs bundled in the same
program archive. For archive programs the census also computes the loader
closure of the entry executable (the files the Windows loader will actually
pull in at start), and splits findings into IN-CLOSURE (on the load path)
vs OUT-OF-CLOSURE (dead files the program ships but never loads).

A missing REGULAR import in the closure is a guaranteed load failure.
A missing DELAY import fails only when the code path that uses it runs.
Resolution does NOT prove a function works (it may still be a runtime
stub); this census measures the "on paper" surface only.

Usage:
  pe_census.py --runtime-dir DIR --games-dir DIR --out DIR

The runtime dir must contain the staged runtime zips (wine64.zip,
wine32.zip, prefix64.zip, prefix32.zip, ...). Demo definitions mirror
web/demos.mjs in this tree.
"""
import argparse
import json
import os
import struct
import sys
import zipfile
from collections import defaultdict

PE_EXTS = (".exe", ".dll", ".ocx", ".sys", ".cpl", ".drv", ".scr", ".exe16")
MACHINE_X86 = 0x014C
MACHINE_X64 = 0x8664


class PEError(Exception):
    pass


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


class PE:
    def __init__(self, data):
        self.data = data
        if len(data) < 0x40 or data[0:2] != b"MZ":
            raise PEError("no MZ")
        peoff = u32(data, 0x3C)
        if peoff + 24 > len(data) or data[peoff:peoff + 4] != b"PE\0\0":
            raise PEError("no PE sig")
        self.machine = u16(data, peoff + 4)
        nsec = u16(data, peoff + 6)
        optsize = u16(data, peoff + 20)
        opt = peoff + 24
        self.magic = u16(data, opt)
        if self.magic == 0x20B:
            self.plus = True
            dd = opt + 112
        elif self.magic == 0x10B:
            self.plus = False
            dd = opt + 96
        else:
            raise PEError("bad optional magic")
        self.dirs = []
        for i in range(16):
            self.dirs.append((u32(data, dd + i * 8), u32(data, dd + i * 8 + 4)))
        secoff = opt + optsize
        self.sections = []
        for i in range(nsec):
            o = secoff + i * 40
            if o + 40 > len(data):
                break
            vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
            self.sections.append((vaddr, vsize, raddr, rsize))

    def rva2off(self, rva):
        for vaddr, vsize, raddr, rsize in self.sections:
            span = max(vsize, rsize)
            if vaddr <= rva < vaddr + span:
                off = raddr + (rva - vaddr)
                return off if off < len(self.data) else None
        if rva < len(self.data):
            return rva
        return None

    def cstr(self, rva, maxlen=1024):
        off = self.rva2off(rva)
        if off is None:
            return None
        end = self.data.find(b"\0", off, off + maxlen)
        if end < 0:
            return None
        try:
            return self.data[off:end].decode("ascii")
        except UnicodeDecodeError:
            return None

    def exports(self):
        """-> (names:set[str], nfuncs:int, forwarders:dict[str,str])"""
        rva, size = self.dirs[0]
        names, forwarders = set(), {}
        if rva == 0:
            return names, 0, forwarders
        off = self.rva2off(rva)
        if off is None or off + 40 > len(self.data):
            return names, 0, forwarders
        d = self.data
        nfunc = u32(d, off + 20)
        nname = u32(d, off + 24)
        afunc = u32(d, off + 28)
        aname = u32(d, off + 32)
        aord = u32(d, off + 36)
        aname_off = self.rva2off(aname)
        aord_off = self.rva2off(aord)
        afunc_off = self.rva2off(afunc)
        if aname_off is None or aord_off is None or afunc_off is None:
            return names, nfunc, forwarders
        name_by_idx = {}
        for i in range(nname):
            nm = self.cstr(u32(d, aname_off + i * 4))
            idx = u16(d, aord_off + i * 2)
            if nm:
                name_by_idx[idx] = nm
                names.add(nm)
        for i in range(nfunc):
            frva = u32(d, afunc_off + i * 4)
            if rva <= frva < rva + size:
                fwd = self.cstr(frva)
                nm = name_by_idx.get(i)
                if nm and fwd:
                    forwarders[nm] = fwd
        return names, nfunc, forwarders

    def imports(self):
        """-> list of (dll, kind, value, delay); kind in {'name','ord'}."""
        out = []
        rva, _ = self.dirs[1]
        if rva:
            off = self.rva2off(rva)
            d = self.data
            while off is not None and off + 20 <= len(d):
                oft = u32(d, off)
                name_rva = u32(d, off + 12)
                ft = u32(d, off + 16)
                if oft == 0 and name_rva == 0 and ft == 0:
                    break
                dll = self.cstr(name_rva) or "?"
                self._thunks(dll, oft or ft, False, out)
                off += 20
        rva, _ = self.dirs[13]
        if rva:
            off = self.rva2off(rva)
            d = self.data
            while off is not None and off + 32 <= len(d):
                name_rva = u32(d, off + 4)
                int_rva = u32(d, off + 16)
                if name_rva == 0:
                    break
                dll = self.cstr(name_rva) or "?"
                self._thunks(dll, int_rva, True, out)
                off += 32
        return out

    def imported_dlls(self):
        """DLL names from both import directories (for closure walking)."""
        dlls = set()
        for diridx, name_off, terminator in ((1, 12, 20), (13, 4, 32)):
            rva, _ = self.dirs[diridx]
            if not rva:
                continue
            off = self.rva2off(rva)
            d = self.data
            while off is not None and off + terminator <= len(d):
                name_rva = u32(d, off + name_off)
                if diridx == 1 and name_rva == 0 and u32(d, off) == 0 and u32(d, off + 16) == 0:
                    break
                if name_rva == 0:
                    break
                nm = self.cstr(name_rva)
                if nm:
                    dlls.add(norm_dll(nm))
                off += terminator
        return dlls

    def _thunks(self, dll, thunk_rva, delay, out):
        d = self.data
        off = self.rva2off(thunk_rva)
        if off is None:
            return
        sz = 8 if self.plus else 4
        ord_flag = 0x8000000000000000 if self.plus else 0x80000000
        while off + sz <= len(d):
            v = u64(d, off) if self.plus else u32(d, off)
            if v == 0:
                break
            if v & ord_flag:
                out.append((dll, "ord", v & 0xFFFF, delay))
            else:
                nm = self.cstr(v + 2)
                if nm:
                    out.append((dll, "name", nm, delay))
            off += sz


def dll_key(name):
    return os.path.basename(name.replace("\\", "/")).lower()


def is_apiset(name):
    return name.startswith("api-ms-") or name.startswith("ext-ms-")


def norm_dll(name):
    n = dll_key(name)
    if "." not in n:
        n += ".dll"
    return n


class Universe:
    """Export tables of every PE the runtime stages, keyed (machine, basename)."""

    def __init__(self):
        self.exports = {}
        self.origin = {}

    def add_zip(self, path):
        zname = os.path.basename(path)
        try:
            z = zipfile.ZipFile(path)
        except Exception as e:
            print(f"WARN: cannot open {path}: {e}", file=sys.stderr)
            return
        for info in z.infolist():
            low = info.filename.lower()
            if not low.endswith(PE_EXTS):
                continue
            key_dll = dll_key(info.filename)
            try:
                pe = PE(z.read(info))
            except Exception:
                continue
            key = (pe.machine, key_dll)
            if key in self.exports:
                continue
            names, _nf, fwd = pe.exports()
            self.exports[key] = (names, fwd)
            self.origin[key] = zname

    def resolve(self, machine, dll, func, extra=None, depth=0, seen=None):
        """-> OK, OK-FWD, APISET, MISSING-DLL, MISSING-FUNC"""
        if is_apiset(dll):
            return "APISET"
        seen = seen or set()
        if (machine, dll, func) in seen or depth > 8:
            return "MISSING-FUNC"
        seen.add((machine, dll, func))
        entry = None
        if extra and (machine, dll) in extra:
            entry = extra[(machine, dll)]
        elif (machine, dll) in self.exports:
            entry = self.exports[(machine, dll)]
        if entry is None:
            return "MISSING-DLL"
        names, fwd = entry
        if func in names and func not in fwd:
            return "OK"
        if func in fwd:
            target = fwd[func]
            if "." not in target:
                return "MISSING-FUNC"
            tdll, tfunc = target.rsplit(".", 1)
            tdll = norm_dll(tdll)
            if tfunc.startswith("#"):
                return "OK-FWD"
            st = self.resolve(machine, tdll, tfunc, extra, depth + 1, seen)
            return "OK-FWD" if st in ("OK", "OK-FWD", "APISET") else st
        return "MISSING-FUNC"


DEMOS = [
    ("notepad", None, "notepad.exe", MACHINE_X64),
    ("explorer", None, "explorer.exe", MACHINE_X64),
    ("snake", None, "snake.exe", MACHINE_X64),
    ("tetris", None, "tetris.exe", MACHINE_X64),
    ("doom", None, "doom.exe", MACHINE_X64),
    ("celeste", "celeste-drmfree.zip", "Celeste.exe", MACHINE_X86),
    ("chromium", "chromium.zip", "chrome.exe", MACHINE_X86),
    ("portal", "portal-stored.zip", "hl2.exe", MACHINE_X86),
    ("gangbeasts", "gangbeasts.zip", "Gang Beasts.exe", MACHINE_X64),
    ("quake3", "q3.zip", "quake3e.x64.exe", MACHINE_X64),
    ("mindustry", "mindustry.zip", "Mindustry.exe", MACHINE_X64),
    ("winmerge", "winmerge.zip", "WinMerge/WinMergeU.exe", MACHINE_X86),
    ("alkhaser", "alkhaser.zip", "al-khaser.exe", MACHINE_X64),
    ("godot-squash", "godot-squash.zip", "SquashTheCreeps.exe", MACHINE_X64),
    ("unigine-tropics", "unigine-tropics.zip", "Tropics/Tropics.exe", MACHINE_X86),
]


def loader_closure(entry_name, blobs_by_name):
    """Names of bundled files reachable from the entry exe via DLL imports."""
    by_dll = {}
    for name, pe in blobs_by_name.items():
        by_dll.setdefault((pe.machine, dll_key(name)), name)
    seen = set()
    stack = [entry_name]
    while stack:
        cur = stack.pop()
        if cur in seen or cur not in blobs_by_name:
            continue
        seen.add(cur)
        pe = blobs_by_name[cur]
        for dl in pe.imported_dlls():
            tgt = by_dll.get((pe.machine, dl))
            if tgt and tgt not in seen:
                stack.append(tgt)
    return seen


def census_program(label, pe_blobs, universe, entry_name=None):
    """pe_blobs: list of (member_name, PE)."""
    blobs_by_name = {name: pe for name, pe in pe_blobs}
    bundled = {}
    for name, pe in pe_blobs:
        if name.lower().endswith((".dll", ".drv", ".cpl", ".ocx")):
            names, _nf, fwd = pe.exports()
            bundled[(pe.machine, dll_key(name))] = (names, fwd)
    closure = None
    if entry_name and entry_name in blobs_by_name:
        closure = loader_closure(entry_name, blobs_by_name)
    stats = defaultdict(int)
    missing = []  # (scope, delay, kind, dll, func, importer)
    apiset_dlls = set()
    for name, pe in pe_blobs:
        scope = "all"
        if closure is not None:
            scope = "in" if name in closure else "out"
        for dll, kind, val, delay in pe.imports():
            dl = norm_dll(dll)
            tag = f"{scope}/{'delay' if delay else 'reg'}"
            if kind == "ord":
                stats[f"ordinal/{tag}"] += 1
                continue
            stats[f"named/{tag}"] += 1
            st = universe.resolve(pe.machine, dl, val, extra=bundled)
            stats[f"{st}/{tag}"] += 1
            if st == "APISET":
                apiset_dlls.add(dl)
            elif st in ("MISSING-DLL", "MISSING-FUNC"):
                missing.append((scope, delay, st, dl, val if st == "MISSING-FUNC" else None, name))
    agg = defaultdict(lambda: defaultdict(set))
    for scope, delay, st, dl, func, importer in missing:
        key = (scope, delay, st, dl, func)
        agg[key]["importers"].add(importer)
    missing_list = [
        {"scope": k[0], "delay": k[1], "kind": k[2], "dll": k[3], "func": k[4],
         "importers": sorted(v["importers"])}
        for k, v in sorted(agg.items(), key=lambda kv: (kv[0][0], kv[0][1], kv[0][2], kv[0][3], kv[0][4] or ""))
    ]
    return {
        "label": label,
        "pe_files": len(pe_blobs),
        "closure_files": (len(closure) if closure is not None else None),
        "stats": dict(stats),
        "missing": missing_list,
        "apiset_dlls": sorted(apiset_dlls),
    }


def load_pe(data):
    try:
        return PE(data)
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime-dir", required=True)
    ap.add_argument("--games-dir", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    universe = Universe()
    runtime_zips = []
    for fn in sorted(os.listdir(args.runtime_dir)):
        if fn.endswith(".zip"):
            runtime_zips.append(os.path.join(args.runtime_dir, fn))
            universe.add_zip(os.path.join(args.runtime_dir, fn))
    print(f"universe: {len(universe.exports)} (arch, name) export tables "
          f"from {len(runtime_zips)} runtime zips", file=sys.stderr)

    report = {"universe_tables": len(universe.exports), "programs": {}}

    selfcheck_blobs = []
    for path in runtime_zips:
        z = zipfile.ZipFile(path)
        for info in z.infolist():
            if not info.filename.lower().endswith(PE_EXTS):
                continue
            try:
                pe = load_pe(z.read(info))
            except Exception:
                pe = None
            if pe:
                selfcheck_blobs.append((f"{os.path.basename(path)}:{info.filename}", pe))
    sc = census_program("runtime-selfcheck", selfcheck_blobs, universe)
    report["programs"]["runtime-selfcheck"] = sc
    print(f"selfcheck: {sc['pe_files']} PEs, stats={sc['stats']}", file=sys.stderr)

    builtin_targets = {exe.lower(): did for did, arc, exe, _m in DEMOS if arc is None}
    builtin_found = {}
    for path in runtime_zips:
        z = zipfile.ZipFile(path)
        for info in z.infolist():
            base = os.path.basename(info.filename).lower()
            if base in builtin_targets and info.filename.lower().endswith(".exe"):
                did = builtin_targets[base]
                if did not in builtin_found:
                    try:
                        pe = load_pe(z.read(info))
                    except Exception:
                        pe = None
                    if pe:
                        builtin_found[did] = (f"{os.path.basename(path)}:{info.filename}", pe)
    for did, arc, exe, machine in DEMOS:
        if arc is None:
            if did in builtin_found:
                name, pe = builtin_found[did]
                rep = census_program(did, [(name, pe)], universe)
                rep["entry"] = name
            else:
                rep = {"label": did, "pe_files": 0, "stats": {},
                       "error": f"builtin exe {exe} not found in any runtime zip"}
            report["programs"][did] = rep
            print(f"{did}: builtin -> {rep.get('stats', rep.get('error'))}", file=sys.stderr)
            continue
        path = os.path.join(args.games_dir, arc)
        if not os.path.exists(path):
            report["programs"][did] = {"label": did, "error": f"archive {arc} not found"}
            print(f"{did}: archive missing", file=sys.stderr)
            continue
        z = zipfile.ZipFile(path)
        blobs = []
        entry_name = None
        for info in z.infolist():
            if not info.filename.lower().endswith(PE_EXTS):
                continue
            try:
                pe = load_pe(z.read(info))
            except Exception:
                pe = None
            if pe:
                blobs.append((info.filename, pe))
                if info.filename.lower().endswith(exe.lower()):
                    entry_name = info.filename
        rep = census_program(did, blobs, universe, entry_name=entry_name)
        rep["entry_found"] = entry_name is not None
        rep["archive"] = arc
        report["programs"][did] = rep
        print(f"{did}: {rep['pe_files']} PEs, closure={rep['closure_files']}, "
              f"stats={rep['stats']}", file=sys.stderr)

    with open(os.path.join(args.out, "pe-census.json"), "w") as f:
        json.dump(report, f, indent=1, sort_keys=True)
    print(f"wrote {os.path.join(args.out, 'pe-census.json')}", file=sys.stderr)


if __name__ == "__main__":
    main()
