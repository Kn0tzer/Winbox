#!/usr/bin/env python3
"""dll_export_census.py - export-implementation census for staged Windows DLLs.

pe_census.py answers the loader question: does every (DLL, function) pair a
program imports EXIST in the staged runtime? This census answers the next
question: does the export a program resolves to actually DO anything, or is
it a Wine "unimplemented stub" - a function that loads fine and then fails
the moment it is called ("Unimplemented function X.Y called")?

Detection. Wine's build tool (winebuild) generates every spec `stub` entry
as the same tiny body: it loads the DLL's own name and the function's own
name as the two arguments and calls/jumps to the DLL's copy of
__wine_spec_unimplemented_stub. The census pattern-matches that generated
body (x86-64 and i386 shapes) and then VALIDATES it against the binary
itself: the embedded module string must equal the DLL's own export-
directory name and the embedded function string must equal the export's
name. A pattern hit that fails validation is reported as unvalidated and
never counted as a stub, so the stub count is a floor established by
self-confirming evidence, not by byte-shape guessing.

Classes per export slot:
  forwarder  - export address points into the export directory (a string
               naming another DLL's function); provided by that DLL.
  stub       - validated unimplemented-stub body (see above).
  code       - any other body in an executable section: real code, an
               import thunk, or (win32u) a syscall entry. Provided.
  data       - address in a non-executable section. Provided (it IS the
               implementation: a variable or table).
  empty      - zero address (an empty slot in the address table).

What this census cannot see, stated plainly: functions implemented in C
as "semi-stubs" (a FIXME log plus a plausible return value). Those are
behaviour questions for guest probes, not for a static census. API-set
DLLs are almost entirely forwarders and are counted as such. Ordinal-only
imports are counted, not name-checked (same rule as pe_census).

Usage:
  dll_export_census.py --runtime-dir DIR [--games-dir DIR] --out DIR
  dll_export_census.py --runtime-dir DIR --write-baseline FILE
  dll_export_census.py --runtime-dir DIR --check --baseline FILE
  dll_export_census.py --self-test

--runtime-dir is a directory holding the staged runtime zips (the same
input pe_census.py takes). The censused set is fixed by design (see
INPUT_ZIPS below): the two wine zips plus the two prefix zips, whose
override copies (e.g. DXVK) win their (arch, DLL) pair - mirroring
pe_census precedence and the runtime's own staging order. Other zips a
deployment may serve from the same directory are not the system
libraries and are ignored.

--check exit codes: 0 = census matches the baseline; 1 = drift (a stub
appeared, a stub disappeared, or a count changed - regenerate the
baseline in the same change that causes the drift, so the register
record shows it); 2 = the wine zips are a different vintage than the
baseline (sha256 mismatch) - re-run the census and regenerate the
baseline deliberately for the new vintage. (The prefix zips are not
hash-gated: they also carry deploy-generated registry files. Their
DLL tables are compared exactly by the drift diff instead.)
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import zipfile
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_census import (DEMOS, MACHINE_X64, MACHINE_X86, PE, PEError,  # noqa: E402
                       dll_key, is_apiset, loader_closure, norm_dll, u16, u32, u64)

DLL_EXTS = (".dll", ".ocx", ".cpl", ".drv")
# The censused input set, by design: the two wine zips are the shipped
# system libraries; the two prefix zips are the only other staged zips
# that can carry override copies of them. A runtime directory may hold
# further content zips (served assets, add-on packs); those are not the
# system libraries and are ignored here. Vintage gating covers all four.
INPUT_ZIPS = ("prefix32.zip", "prefix64.zip", "wine32.zip", "wine64.zip")
PRIMARY_ZIPS = ("wine64.zip", "wine32.zip")
SEC_EXEC = 0x20000000

# winebuild's unimplemented-stub bodies.
# x86-64: sub rsp,0x28 ; lea rcx,[rip+..] (module) ; lea rdx,[rip+..] (func) ;
#         call|jmp __wine_spec_unimplemented_stub
X64_PRE = bytes.fromhex("4883ec28488d0d")
X64_MID = bytes.fromhex("488d15")
# i386 (after up to 16 bytes of 0x90 alignment padding):
#   sub esp,0xc ; mov [esp+4],<func VA> ; mov [esp],<module VA> ;
#   call|jmp __wine_spec_unimplemented_stub
X86_PRE = bytes.fromhex("83ec0cc7442404")
X86_MID = bytes.fromhex("c70424")


def image_base(pe):
    d = pe.data
    peoff = u32(d, 0x3C)
    opt = peoff + 24
    if pe.plus:
        return u64(d, opt + 24)
    return u32(d, opt + 28)


def section_chars(pe):
    """(vaddr, span, characteristics) per section."""
    d = pe.data
    peoff = u32(d, 0x3C)
    nsec = u16(d, peoff + 6)
    optsize = u16(d, peoff + 20)
    secoff = peoff + 24 + optsize
    out = []
    for i in range(nsec):
        o = secoff + i * 40
        if o + 40 > len(d):
            break
        vsize, vaddr = struct.unpack_from("<II", d, o + 8)
        rsize = u32(d, o + 16)
        chars = u32(d, o + 36)
        out.append((vaddr, max(vsize, rsize), chars))
    return out


class DllCensus:
    """Classification of one PE's export table."""

    def __init__(self, pe):
        self.pe = pe
        self.module = None       # the DLL's own export-directory name
        self.slots = 0
        self.named = 0
        self.forwarders = {}
        self.stubs = {}          # export name -> embedded name (== name)
        self.stub_slots = 0      # stub slots incl. ordinal-only
        self.unvalidated = []    # pattern hits that failed string validation
        self.code = 0
        self.data = 0
        self.empty = 0
        self.helper_targets = set()
        self._parse()

    def _parse(self):
        pe = self.pe
        d = pe.data
        rva, size = pe.dirs[0]
        if rva == 0:
            return
        off = pe.rva2off(rva)
        if off is None or off + 40 > len(d):
            return
        self.module = pe.cstr(u32(d, off + 12))
        nfunc = u32(d, off + 20)
        nname = u32(d, off + 24)
        afunc = u32(d, off + 28)
        aname = u32(d, off + 32)
        aord = u32(d, off + 36)
        afunc_off = pe.rva2off(afunc)
        aname_off = pe.rva2off(aname)
        aord_off = pe.rva2off(aord)
        if afunc_off is None or aname_off is None or aord_off is None:
            return
        self.slots = nfunc
        name_by_idx = {}
        for i in range(nname):
            nm = pe.cstr(u32(d, aname_off + i * 4))
            idx = u16(d, aord_off + i * 2)
            if nm:
                name_by_idx[idx] = nm
        self.named = len(name_by_idx)
        secs = section_chars(pe)
        base = image_base(pe)
        for i in range(nfunc):
            frva = u32(d, afunc_off + i * 4)
            nm = name_by_idx.get(i)
            if frva == 0:
                self.empty += 1
                continue
            if rva <= frva < rva + size:
                fwd = pe.cstr(frva)
                if nm and fwd:
                    self.forwarders[nm] = fwd
                continue
            exec_sec = any(va <= frva < va + span and (ch & SEC_EXEC)
                           for va, span, ch in secs)
            if not exec_sec:
                self.data += 1
                continue
            foff = pe.rva2off(frva)
            if foff is None:
                self.data += 1
                continue
            stub = self._stub_at(frva, d[foff:foff + 48], base)
            if stub is not None:
                mod_s, fn_s, target = stub
                self.helper_targets.add(target)
                if (self.module and mod_s == self.module
                        and (nm is None or fn_s == nm)):
                    self.stub_slots += 1
                    if nm:
                        self.stubs[nm] = fn_s
                    continue
                self.unvalidated.append(nm or f"#{i}")
            self.code += 1

    def _stub_at(self, frva, body, base):
        """(module_string, func_string, call_target_rva) or None."""
        pe = self.pe
        if pe.plus:
            if body[:7] != X64_PRE or body[11:14] != X64_MID or body[18] not in (0xE8, 0xE9):
                return None
            rcx_rel = struct.unpack_from("<i", body, 7)[0]
            rdx_rel = struct.unpack_from("<i", body, 14)[0]
            call_rel = struct.unpack_from("<i", body, 19)[0]
            mod_s = pe.cstr(frva + 11 + rcx_rel)
            fn_s = pe.cstr(frva + 18 + rdx_rel)
            target = frva + 18 + 5 + call_rel
            return (mod_s, fn_s, target)
        b = body
        i = 0
        while i < 16 and i < len(b) and b[i] == 0x90:
            i += 1
        b2 = b[i:]
        if b2[:7] != X86_PRE or b2[11:14] != X86_MID or b2[18] not in (0xE8, 0xE9):
            return None
        # winebuild emits the [esp+4] store first: that slot is the SECOND
        # argument (the function name); the [esp] store is the module name.
        # (Verified against shipped advapi32: [esp+4] -> "CredProfileLoaded",
        # [esp] -> "advapi32.dll". The x86-64 shape uses rcx/rdx in order.)
        fn_va = struct.unpack_from("<I", b2, 7)[0]
        mod_va = struct.unpack_from("<I", b2, 14)[0]
        call_rel = struct.unpack_from("<i", b2, 19)[0]
        mod_s = pe.cstr(mod_va - base)
        fn_s = pe.cstr(fn_va - base)
        target = frva + i + 18 + 5 + call_rel
        return (mod_s, fn_s, target)

    def summary(self):
        return {
            "module": self.module,
            "slots": self.slots,
            "named": self.named,
            "forwarders": len(self.forwarders),
            "stubs": self.stub_slots,
            "stub_named": len(self.stubs),
            "code": self.code,
            "data": self.data,
            "empty": self.empty,
            "unvalidated": len(self.unvalidated),
        }


def census_zip(path):
    """-> (sha256, {(machine, dllkey): (origin_member, DllCensus)})"""
    h = hashlib.sha256()
    tables = {}
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    z = zipfile.ZipFile(path)
    for info in z.infolist():
        if not info.filename.lower().endswith(DLL_EXTS):
            continue
        try:
            pe = PE(z.read(info))
        except Exception:
            continue
        key = (pe.machine, dll_key(info.filename))
        if key in tables:
            continue
        tables[key] = (info.filename, DllCensus(pe))
    return h.hexdigest(), tables


def runtime_census(runtime_dir):
    """Census the defined input set (INPUT_ZIPS) of a runtime dir.

    Returns (zip_hashes, tables) where tables maps (machine, dllkey) ->
    {origin_zip, member, census}. Prefix zips may ADD override tables
    for (machine, dll) pairs they carry; mirroring pe_census and the
    runtime staging order, the first zip in sorted order wins a pair.
    """
    zip_hashes = {}
    tables = {}
    for fn in INPUT_ZIPS:
        path = os.path.join(runtime_dir, fn)
        if not os.path.exists(path):
            continue
        sha, zt = census_zip(path)
        zip_hashes[fn] = sha
        for key, (member, cen) in zt.items():
            if key not in tables:
                tables[key] = {"origin_zip": fn, "member": member, "census": cen}
    return zip_hashes, tables


def baseline_of(zip_hashes, tables):
    dlls = {}
    tot = defaultdict(int)
    for (machine, dll), rec in sorted(tables.items()):
        cen = rec["census"]
        s = cen.summary()
        arch = "x64" if machine == MACHINE_X64 else "x86"
        dlls[f"{arch}/{dll}"] = {
            "origin_zip": rec["origin_zip"],
            "slots": s["slots"], "named": s["named"],
            "forwarders": s["forwarders"], "stubs": s["stubs"],
            "stub_names": sorted(cen.stubs),
            "code": s["code"], "data": s["data"], "empty": s["empty"],
            "unvalidated": s["unvalidated"],
        }
        for k in ("slots", "named", "forwarders", "stubs", "code", "data", "empty"):
            tot[k] += s[k]
        tot["dlls"] += 1
    return {
        "format": 1,
        "zip_sha256": dict(zip_hashes),
        "totals": dict(tot),
        "dlls": dlls,
    }


def diff_baseline(old, new):
    """List of human-readable drift lines; empty means identical."""
    out = []
    if old["totals"] != new["totals"]:
        out.append(f"totals differ: baseline {old['totals']} vs census {new['totals']}")
    od, nd = old["dlls"], new["dlls"]
    for key in sorted(set(od) | set(nd)):
        if key not in od:
            out.append(f"{key}: new DLL table (in census, not in baseline)")
            continue
        if key not in nd:
            out.append(f"{key}: DLL table missing from census")
            continue
        o, n = od[key], nd[key]
        for f in ("slots", "named", "forwarders", "stubs", "code", "data", "empty"):
            if o[f] != n[f]:
                out.append(f"{key}: {f} {o[f]} -> {n[f]}")
        o_stubs, n_stubs = set(o["stub_names"]), set(n["stub_names"])
        for fn in sorted(n_stubs - o_stubs):
            out.append(f"{key}: NEW STUB {fn}")
        for fn in sorted(o_stubs - n_stubs):
            out.append(f"{key}: stub resolved {fn} (regenerate the baseline to record the fix)")
    return out


# ---------------------------------------------------------------- demos

def classify_tables(tables):
    """(machine, dll) -> (export_names, forwarders, stub_names) for resolution."""
    out = {}
    for key, rec in tables.items():
        cen = rec["census"]
        pe = cen.pe
        names, _nf, fwd = pe.exports()
        out[key] = (names, fwd, set(cen.stubs))
    return out


def resolve_stub(lookup, machine, dll, func, extra=None, depth=0, seen=None):
    """-> OK, STUB, APISET, MISSING-DLL, MISSING-FUNC (forwarder-chasing)."""
    if is_apiset(dll):
        return "APISET"
    seen = seen or set()
    if (machine, dll, func) in seen or depth > 8:
        return "MISSING-FUNC"
    seen.add((machine, dll, func))
    entry = None
    if extra and (machine, dll) in extra:
        entry = extra[(machine, dll)]
    elif (machine, dll) in lookup:
        entry = lookup[(machine, dll)]
    if entry is None:
        return "MISSING-DLL"
    names, fwd, stubs = entry
    if func in fwd:
        target = fwd[func]
        if "." not in target:
            return "MISSING-FUNC"
        tdll, tfunc = target.rsplit(".", 1)
        if tfunc.startswith("#"):
            return "OK"
        return resolve_stub(lookup, machine, norm_dll(tdll), tfunc, extra, depth + 1, seen)
    if func in names:
        return "STUB" if func in stubs else "OK"
    return "MISSING-FUNC"


def demo_crossref(runtime_dir, games_dir, tables):
    """Imports of the demo programs, classified with stub awareness."""
    lookup = classify_tables(tables)
    report = {}

    def census_blobs(label, blobs, entry_name=None):
        by_name = {n: p for n, p in blobs}
        bundled = {}
        for name, pe in blobs:
            if name.lower().endswith(DLL_EXTS):
                names, _nf, fwd = pe.exports()
                stubs = set(DllCensus(pe).stubs)
                bundled[(pe.machine, dll_key(name))] = (names, fwd, stubs)
        closure = None
        if entry_name and entry_name in by_name:
            closure = loader_closure(entry_name, by_name)
        stats = defaultdict(int)
        stub_hits, missing = [], []
        for name, pe in blobs:
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
                st = resolve_stub(lookup, pe.machine, dl, val, extra=bundled)
                stats[f"{st}/{tag}"] += 1
                if st == "STUB":
                    stub_hits.append({"scope": scope, "delay": delay, "dll": dl,
                                      "func": val, "importer": name})
                elif st in ("MISSING-DLL", "MISSING-FUNC"):
                    missing.append({"scope": scope, "delay": delay, "kind": st,
                                    "dll": dl, "func": val, "importer": name})
        return {"pe_files": len(blobs),
                "closure_files": (len(closure) if closure is not None else None),
                "stats": dict(stats), "stub_imports": stub_hits, "missing": missing}

    runtime_zips = [os.path.join(runtime_dir, fn)
                    for fn in sorted(os.listdir(runtime_dir)) if fn.endswith(".zip")]
    builtin_targets = {exe.lower(): did for did, arc, exe, _m in DEMOS if arc is None}
    builtin_found = {}
    game_blobs = {}
    for path in runtime_zips:
        z = zipfile.ZipFile(path)
        for info in z.infolist():
            base = os.path.basename(info.filename).lower()
            if base in builtin_targets and info.filename.lower().endswith(".exe"):
                did = builtin_targets[base]
                if did not in builtin_found:
                    try:
                        builtin_found[did] = (f"{os.path.basename(path)}:{info.filename}",
                                              PE(z.read(info)))
                    except Exception:
                        pass
    for did, arc, exe, machine in DEMOS:
        if arc is None:
            if did in builtin_found:
                name, pe = builtin_found[did]
                rep = census_blobs(did, [(name, pe)])
                rep["entry"] = name
            else:
                rep = {"error": f"builtin exe {exe} not found in any runtime zip"}
            report[did] = rep
            continue
        path = os.path.join(games_dir, arc)
        if not os.path.exists(path):
            report[did] = {"error": f"archive {arc} not found"}
            continue
        z = zipfile.ZipFile(path)
        blobs, entry_name = [], None
        for info in z.infolist():
            if not info.filename.lower().endswith((".exe",) + DLL_EXTS + (".sys", ".cpl", ".scr")):
                continue
            try:
                pe = PE(z.read(info))
            except Exception:
                continue
            blobs.append((info.filename, pe))
            if info.filename.lower().endswith(exe.lower()):
                entry_name = info.filename
        rep = census_blobs(did, blobs, entry_name=entry_name)
        rep["archive"] = arc
        rep["entry_found"] = entry_name is not None
        report[did] = rep
    return report


# ---------------------------------------------------------------- self-test

def _synthetic_pe(plus, exports_spec):
    """Build a minimal PE in memory.

    exports_spec: list of (name, kind), kind in {'stub', 'code', 'data',
    'fwd'}; forwarders get the target 'ntdll.RtlFoo'. Layout: headers in
    the first page, .text at RVA 0x1000, .rdata at RVA 0x2000 holding all
    strings first (their RVAs are known before any body is built), then
    the export directory and tables, then the data export's bytes.
    """
    image_base = 0x140000000 if plus else 0x10000000
    n = len(exports_spec)

    # The export directory block (struct + 3 tables) comes first in .rdata
    # so the directory's declared range covers the strings that follow it,
    # as in a real linker-produced export section.
    dir_block = 40 + 4 * n + 4 * n + 2 * n
    str_base = 0x2000 + dir_block
    _next = [str_base]

    def rstr(s):
        rva = _next[0]
        _next[0] += len(s.encode()) + 1
        return rva

    module_rva = rstr("selftest.dll")
    func_str = {name: rstr(name) for name, kind in exports_spec if kind == "stub"}
    fwd_str = {name: rstr("ntdll.RtlFoo") for name, kind in exports_spec if kind == "fwd"}
    name_str = {name: rstr(name) for name, kind in exports_spec}
    data_rva = 0x3000  # its own non-executable section, as in real DLLs

    # .text: reserve the helper first so stub call offsets are computable
    # after bodies are laid out; bodies come first, helper last.
    text = bytearray()
    body_rva = {}
    stub_at = []  # (name, text_offset, body_rva)
    for name, kind in exports_spec:
        rva = 0x1000 + len(text)
        body_rva[name] = rva
        if kind == "stub":
            stub_at.append((name, len(text), rva))
            text += bytes(32)
        elif kind == "code":
            if plus:
                text += bytes.fromhex("48894c2408534883ec20488bd94883c4205bc3") + b"\x90" * 13
            else:
                text += b"\x55\x8b\xec\x5d\xc3" + b"\x90" * 11
        else:
            text += b"\x90" * 16  # fwd/data slots never point here
    helper_rva = 0x1000 + len(text)
    text += b"\xc3" + b"\x90" * 15
    for name, toff, rva in stub_at:
        if plus:
            body = bytes.fromhex("4883ec28488d0d")
            body += struct.pack("<i", module_rva - (rva + 11))
            body += bytes.fromhex("488d15")
            body += struct.pack("<i", func_str[name] - (rva + 18))
            body += b"\xe8" + struct.pack("<i", helper_rva - (rva + 23))
            body += b"\x90" * 7
        else:
            # mirror winebuild's real emit order: [esp+4] = function name
            # (second argument), [esp] = module name (first argument).
            body = bytes.fromhex("83ec0cc7442404")
            body += struct.pack("<I", image_base + func_str[name])
            body += bytes.fromhex("c70424")
            body += struct.pack("<I", image_base + module_rva)
            body += b"\xe8" + struct.pack("<i", helper_rva - (rva + 23))
            body += b"\x90" * 9
        text[toff:toff + len(body)] = body

    dir_rva = 0x2000
    addr_table_rva = dir_rva + 40
    name_table_rva = addr_table_rva + 4 * n
    ord_table_rva = name_table_rva + 4 * n
    rdata = bytearray()
    rdata.extend(struct.pack("<IIHHIIIIIII", 0, 0, 0, 0, module_rva, 1,
                             n, n, addr_table_rva, name_table_rva, ord_table_rva))
    for name, kind in exports_spec:
        if kind == "fwd":
            rdata.extend(struct.pack("<I", fwd_str[name]))
        elif kind == "data":
            rdata.extend(struct.pack("<I", data_rva))
        else:
            rdata.extend(struct.pack("<I", body_rva[name]))
    for name, kind in exports_spec:
        rdata.extend(struct.pack("<I", name_str[name]))
    for i in range(n):
        rdata.extend(struct.pack("<H", i))
    for s in (["selftest.dll"]
              + [nm for nm, k in exports_spec if k == "stub"]
              + ["ntdll.RtlFoo" for nm, k in exports_spec if k == "fwd"]
              + [nm for nm, k in exports_spec]):
        rdata.extend(s.encode() + b"\0")

    hdr = bytearray(0x400)
    hdr[0:2] = b"MZ"
    struct.pack_into("<I", hdr, 0x3C, 0x80)
    hdr[0x80:0x84] = b"PE\0\0"
    coff = 0x84
    struct.pack_into("<HH", hdr, coff, 0x8664 if plus else 0x14C, 3)
    opt_size = 0xF0 if plus else 0xE0
    struct.pack_into("<H", hdr, coff + 16, opt_size)
    struct.pack_into("<H", hdr, coff + 18, 0x210E)  # characteristics, cosmetic
    opt = coff + 20
    struct.pack_into("<H", hdr, opt, 0x20B if plus else 0x10B)
    if plus:
        struct.pack_into("<Q", hdr, opt + 24, image_base)
    else:
        struct.pack_into("<I", hdr, opt + 28, image_base)
    dd = opt + (112 if plus else 96)
    struct.pack_into("<II", hdr, dd, dir_rva, len(rdata))
    secoff = opt + opt_size

    def section(o, name, vsize, vaddr, rsize, raddr, chars):
        hdr[o:o + 8] = name.ljust(8, b"\0")
        struct.pack_into("<IIII", hdr, o + 8, vsize, vaddr, rsize, raddr)
        struct.pack_into("<I", hdr, o + 36, chars)

    section(secoff, b".text", len(text), 0x1000, len(text), 0x400, 0x60000020)
    section(secoff + 40, b".rdata", len(rdata), 0x2000, len(rdata),
            0x400 + len(text), 0x40000040)
    section(secoff + 80, b".data", 8, 0x3000, 8,
            0x400 + len(text) + len(rdata), 0xC0000040)
    return bytes(hdr) + bytes(text) + bytes(rdata) + b"\x2a" * 8


def self_test():
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)

    for plus, arch in ((True, "x64"), (False, "x86")):
        spec = [("RealFunc", "code"), ("StubFunc", "stub"),
                ("FwdFunc", "fwd"), ("DataThing", "data")]
        pe = PE(_synthetic_pe(plus, spec))
        cen = DllCensus(pe)
        check(cen.module == "selftest.dll", f"{arch}: module name {cen.module!r}")
        check(sorted(cen.stubs) == ["StubFunc"], f"{arch}: stubs {sorted(cen.stubs)}")
        check(cen.stub_slots == 1, f"{arch}: stub_slots {cen.stub_slots}")
        check(cen.forwarders.get("FwdFunc") == "ntdll.RtlFoo",
              f"{arch}: forwarders {cen.forwarders}")
        check(cen.code == 1, f"{arch}: code {cen.code}")
        check(cen.data == 1, f"{arch}: data {cen.data}")
        check(not cen.unvalidated, f"{arch}: unvalidated {cen.unvalidated}")
    if fails:
        for f in fails:
            print(f"SELF-TEST FAIL: {f}")
        return 1
    print("self-test: classifier verdicts correct on synthetic x64 and x86 PEs")
    return 0


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime-dir")
    ap.add_argument("--games-dir")
    ap.add_argument("--out")
    ap.add_argument("--baseline")
    ap.add_argument("--write-baseline")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.runtime_dir:
        print("error: --runtime-dir is required", file=sys.stderr)
        return 2

    zip_hashes, tables = runtime_census(args.runtime_dir)
    baseline = baseline_of(zip_hashes, tables)
    tot = baseline["totals"]
    named_stub_total = sum(len(v["stub_names"]) for v in baseline["dlls"].values())
    print(f"census: {tot['dlls']} DLL tables, {tot['slots']} export slots, "
          f"{tot['named']} named, {tot['forwarders']} forwarders, "
          f"{tot['stubs']} stub slots ({named_stub_total} named stubs), "
          f"{tot['code']} code, {tot['data']} data")

    if args.write_baseline:
        with open(args.write_baseline, "w") as f:
            json.dump(baseline, f, indent=1, sort_keys=True)
        print(f"wrote baseline {args.write_baseline}")
        return 0

    if args.check:
        if not args.baseline:
            print("error: --check needs --baseline", file=sys.stderr)
            return 2
        with open(args.baseline) as f:
            old = json.load(f)
        # Vintage is defined by the two wine zips (the system libraries).
        # The prefix zips also carry deploy-generated registry files, so
        # their container hashes legitimately differ between stagings;
        # their DLL tables are compared exactly by the drift diff below.
        old_hashes = old.get("zip_sha256", {})
        for fn in PRIMARY_ZIPS:
            sha = old_hashes.get(fn)
            if sha is not None and zip_hashes.get(fn) != sha:
                print(f"VINTAGE-CHANGED: {fn} sha256 differs from the baseline's "
                      f"recorded vintage; re-run the census and regenerate the "
                      f"baseline deliberately for the new runtime vintage.")
                return 2
        drift = diff_baseline(old, baseline)
        if drift:
            print(f"DRIFT: {len(drift)} difference(s) from {args.baseline}:")
            for line in drift[:50]:
                print(f"  {line}")
            if len(drift) > 50:
                print(f"  ... and {len(drift) - 50} more")
            return 1
        print("check: census matches the baseline exactly")
        return 0

    if args.out:
        os.makedirs(args.out, exist_ok=True)
        report = {"baseline": baseline}
        if args.games_dir:
            report["demos"] = demo_crossref(args.runtime_dir, args.games_dir, tables)
            for did, rep in sorted(report["demos"].items()):
                if "stats" in rep:
                    stub_in = sum(1 for h in rep["stub_imports"]
                                  if h["scope"] == "in" and not h["delay"])
                    print(f"demo {did}: stats={rep['stats']} "
                          f"stub-imports(in-closure, regular)={stub_in}")
        with open(os.path.join(args.out, "dll-export-census.json"), "w") as f:
            json.dump(report, f, indent=1, sort_keys=True)
        print(f"wrote {os.path.join(args.out, 'dll-export-census.json')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
