#!/usr/bin/env python3
"""syscall_census.py - Linux syscall surface census for the emulator kernel.

Specification vs implementation, extracted mechanically:

- x86-64: the kernel source carries a complete number->name table for
  logging (x64SyscallName in syscall64.cpp, kept complete through the
  Linux v6.12 table). That is the spec. The implementation is the set of
  `case X64_SYS_*:` labels in the dispatch switch in the same file
  (constants resolved via the file's own #defines).
- i386: the spec is the host cross header unistd_32.h (__NR_* list).
  The implementation is the positional syscallFunc[] table in
  syscall.cpp: a nullptr slot is an unimplemented syscall (the caller
  returns -ENOSYS), a named handler is an implementation.

A syscall with no implementation is surface a guest process can call
that the kernel answers with ENOSYS. The census says nothing about
whether Wine/glibc actually call it; it measures the unexamined surface.

Three-way classification (added for the FD-23 disposition pass):

- x86-64: a dispatch case whose whole body is the central
  unimplementedSyscall64() call is a "stub" — the number is recognised
  and deliberately refused with a logged -ENOSYS (e.g. select, rseq).
  A case that reaches unimplementedSyscall64() only on a fallback path
  is "implemented" but also listed under "conditional_enosys".
  Everything else with a case is "implemented"; named numbers with no
  case at all are "missing" (they reach the same -ENOSYS fallback via
  the switch default, which the dispatch-guard test pins).
- i386: a table slot naming a handler whose whole body returns
  -K_ENOSYS is a "stub" (e.g. clone3); a nullptr slot is "missing";
  numbers past the end of the table are "beyond_table" (the caller
  bounds-checks and answers -ENOSYS).

Usage: syscall_census.py --source-dir DIR --unistd32 PATH --out DIR
         [--dispositions PATH]

The dispositions file (default: syscall_dispositions.json beside this
tool) records, per ABI and number, why each still-unimplemented syscall
is absent: a category (legacy-dead, superseded, privilege-surface,
subsystem-absent, identity-model, ordinary-unimplemented,
modern-unconsumed) and the route by which it would be served. The census
audits the file both ways: gap numbers with no entry are reported as
"undispositioned" (the queue is not fully dispositioned until that count
is 0), and entries for numbers that are no longer in the gap — because
they were implemented meanwhile — are reported as "stale" so the entry
is removed rather than silently drifting from reality.
"""
import argparse
import json
import os
import re
import sys


def brace_body(text, brace_index):
    depth = 0
    i = brace_index
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[brace_index:i]
        i += 1
    return text[brace_index:]


def case_bodies(switch_body):
    """Yield (labels, body) for each case in a switch body."""
    parts = re.split(r"(case\s+[^:]+:)", switch_body)
    # parts[0] is the prelude; then alternating label/body chunks.
    for i in range(1, len(parts) - 1, 2):
        label = parts[i]
        body = parts[i + 1] if i + 1 < len(parts) else ""
        yield label, body


def census_x64(path):
    text = open(path).read()
    # spec: name table
    m = re.search(r"static const char\* x64SyscallName", text)
    body = brace_body(text, text.index("{", m.start()))
    names = {}
    for cm in re.finditer(r"case\s+(\d+)\s*:\s*return\s+\"([^\"]+)\"", body):
        names[int(cm.group(1))] = cm.group(2)
    # constants
    defines = {}
    for dm in re.finditer(r"#define\s+X64_SYS_(\w+)\s+(\d+)", text):
        defines[dm.group(1)] = int(dm.group(2))
    # dispatch: the switch(nr) whose body references X64_SYS_
    dispatched = set()
    stubbed = set()
    conditional = set()
    for sm in re.finditer(r"switch \(nr\) \{", text):
        b = brace_body(text, sm.start() + len("switch (nr) "))
        if "X64_SYS_" not in b:
            continue
        # Group consecutive labels that share one body (case A: case B: body).
        pending = []
        for label, body in case_bodies(b):
            nums = []
            for cm in re.finditer(r"case\s+X64_SYS_(\w+)\s*:", label):
                if cm.group(1) in defines:
                    nums.append(defines[cm.group(1)])
            for cm in re.finditer(r"case\s+(\d+)\s*:", label):
                nums.append(int(cm.group(1)))
            pending.extend(nums)
            if not body.strip():
                continue  # shared-body label; numbers stay pending
            code = re.sub(r"//[^\n]*", "", body)
            code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
            for nr in pending:
                dispatched.add(nr)
                if "unimplementedSyscall64" in code:
                    if re.fullmatch(r"\s*ret\s*=\s*unimplementedSyscall64\(nr\);\s*break;\s*", code):
                        stubbed.add(nr)
                    else:
                        conditional.add(nr)
            pending = []
    stubbed &= dispatched
    conditional &= dispatched
    implemented = dispatched - stubbed
    missing = {nr: names[nr] for nr in sorted(names) if nr not in dispatched}
    return {
        "file": "source/kernel/syscall64.cpp",
        "spec_source": "x64SyscallName table (Linux v6.12, in-file)",
        "named_syscalls": len(names),
        "dispatched": len([nr for nr in names if nr in implemented]),
        "implemented_count": len([nr for nr in names if nr in implemented]),
        "stubbed_count": len([nr for nr in names if nr in stubbed]),
        "stubbed": {nr: names[nr] for nr in sorted(names) if nr in stubbed},
        "conditional_enosys": {nr: names[nr] for nr in sorted(names) if nr in conditional},
        "missing_count": len(missing),
        "missing": missing,
    }


def census_i386(path, unistd32):
    spec = {}
    for line in open(unistd32):
        m = re.match(r"#define\s+__NR_(\w+)\s+(\d+)", line)
        if m:
            spec[int(m.group(2))] = m.group(1)
    text = open(path).read()
    m = re.search(r"static const SyscallFunc syscallFunc\[\] = \{", text)
    raw = brace_body(text, text.index("{", m.start()))
    body = re.sub(r"//[^\n]*", "", raw)  # comments may hold unbalanced parens
    # strip the opening brace; entries are comma separated at depth 1
    inner = body[1:]
    entries = []
    depth = 0
    cur = ""
    for ch in inner:
        if ch in "{(":
            depth += 1
        elif ch in "})":
            depth -= 1
        if ch == "," and depth == 0:
            entries.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        entries.append(cur)
    implemented = set()
    handler_of = {}
    for idx, e in enumerate(entries):
        e = e.strip()
        if not e:
            continue
        handler = e.split("//")[0].strip()
        if handler and handler != "nullptr":
            implemented.add(idx)
            handler_of[idx] = handler
    # A named handler whose whole body is `return -K_ENOSYS;` is a stub:
    # recognised and deliberately refused, not an implementation.
    stubbed = set()
    for idx, handler in handler_of.items():
        hm = re.search(r"(?:^|\n)\s*(?:static\s+)?U32\s+" + re.escape(handler) +
                       r"\s*\(CPU\* cpu, U32 eipCount\)\s*\{", text)
        if not hm:
            continue
        hbody = brace_body(text, hm.end() - 1)
        hcode = re.sub(r"//[^\n]*", "", hbody)
        hcode = re.sub(r"/\*.*?\*/", "", hcode, flags=re.S)
        if re.fullmatch(r"\{\s*return\s+-K_ENOSYS;\s*\}?", hcode.strip()):
            stubbed.add(idx)
    implemented -= stubbed
    missing = {nr: spec[nr] for nr in sorted(spec) if nr not in implemented and nr not in stubbed and nr < len(entries)}
    beyond = {nr: spec[nr] for nr in sorted(spec) if nr >= len(entries)}
    return {
        "file": "source/kernel/syscall.cpp",
        "spec_source": "unistd_32.h (__NR_* list)",
        "named_syscalls": len(spec),
        "table_slots": len(entries),
        "dispatched": len([nr for nr in spec if nr in implemented]),
        "implemented_count": len([nr for nr in spec if nr in implemented]),
        "stubbed_count": len([nr for nr in spec if nr in stubbed]),
        "stubbed": {nr: spec[nr] for nr in sorted(spec) if nr in stubbed},
        "missing_count": len(missing),
        "missing": missing,
        "beyond_table_count": len(beyond),
        "beyond_table": beyond,
    }


def audit_dispositions(report, dispositions):
    """Cross-check the dispositions file against the measured gap."""
    for arch, r in report.items():
        gap = {}
        gap.update(r.get("missing", {}))
        gap.update(r.get("beyond_table", {}))
        entries = dispositions.get(arch, {}) if dispositions else {}
        undispositioned = {nr: gap[nr] for nr in sorted(gap) if str(nr) not in entries}
        stale = {}
        for nr_str, entry in entries.items():
            try:
                nr = int(nr_str)
            except ValueError:
                continue
            if nr not in gap:
                stale[nr] = entry.get("name", "?") if isinstance(entry, dict) else "?"
        r["undispositioned_count"] = len(undispositioned)
        r["undispositioned"] = undispositioned
        r["stale_dispositions_count"] = len(stale)
        r["stale_dispositions"] = {nr: stale[nr] for nr in sorted(stale)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source-dir", required=True)
    ap.add_argument("--unistd32", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dispositions", default=None,
                    help="path to syscall_dispositions.json "
                         "(default: beside this tool)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    kdir = os.path.join(args.source_dir, "source", "kernel")
    report = {
        "x86_64": census_x64(os.path.join(kdir, "syscall64.cpp")),
        "i386": census_i386(os.path.join(kdir, "syscall.cpp"), args.unistd32),
    }
    disp_path = args.dispositions or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "syscall_dispositions.json")
    dispositions = None
    if os.path.exists(disp_path):
        with open(disp_path) as f:
            dispositions = json.load(f)
    audit_dispositions(report, dispositions)
    with open(os.path.join(args.out, "syscall-census.json"), "w") as f:
        json.dump(report, f, indent=1, sort_keys=True)
    for arch, r in report.items():
        print(arch, "->", {k: v for k, v in r.items() if k not in ("missing", "beyond_table", "undispositioned", "stale_dispositions")},
              file=sys.stderr)
        miss = r.get("missing", {})
        print("  missing sample:", ", ".join(f"{nr}:{n}" for nr, n in list(miss.items())[:50]),
              file=sys.stderr)
        print("  undispositioned:", r["undispositioned_count"],
              " stale dispositions:", r["stale_dispositions_count"],
              file=sys.stderr)


if __name__ == "__main__":
    main()
