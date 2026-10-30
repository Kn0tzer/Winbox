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

Usage: syscall_census.py --source-dir DIR --unistd32 PATH --out DIR
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
    for sm in re.finditer(r"switch \(nr\) \{", text):
        b = brace_body(text, sm.start() + len("switch (nr) "))
        if "X64_SYS_" not in b:
            continue
        for cm in re.finditer(r"case\s+X64_SYS_(\w+)\s*:", b):
            if cm.group(1) in defines:
                dispatched.add(defines[cm.group(1)])
        for cm in re.finditer(r"case\s+(\d+)\s*:", b):
            dispatched.add(int(cm.group(1)))
    missing = {nr: names[nr] for nr in sorted(names) if nr not in dispatched}
    return {
        "file": "source/kernel/syscall64.cpp",
        "spec_source": "x64SyscallName table (Linux v6.12, in-file)",
        "named_syscalls": len(names),
        "dispatched": len([nr for nr in names if nr in dispatched]),
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
    for idx, e in enumerate(entries):
        e = e.strip()
        if not e:
            continue
        handler = e.split("//")[0].strip()
        if handler and handler != "nullptr":
            implemented.add(idx)
    missing = {nr: spec[nr] for nr in sorted(spec) if nr not in implemented and nr < len(entries)}
    beyond = {nr: spec[nr] for nr in sorted(spec) if nr >= len(entries)}
    return {
        "file": "source/kernel/syscall.cpp",
        "spec_source": "unistd_32.h (__NR_* list)",
        "named_syscalls": len(spec),
        "table_slots": len(entries),
        "dispatched": len([nr for nr in spec if nr in implemented]),
        "missing_count": len(missing),
        "missing": missing,
        "beyond_table_count": len(beyond),
        "beyond_table": beyond,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source-dir", required=True)
    ap.add_argument("--unistd32", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    kdir = os.path.join(args.source_dir, "source", "kernel")
    report = {
        "x86_64": census_x64(os.path.join(kdir, "syscall64.cpp")),
        "i386": census_i386(os.path.join(kdir, "syscall.cpp"), args.unistd32),
    }
    with open(os.path.join(args.out, "syscall-census.json"), "w") as f:
        json.dump(report, f, indent=1, sort_keys=True)
    for arch, r in report.items():
        print(arch, "->", {k: v for k, v in r.items() if k not in ("missing", "beyond_table")},
              file=sys.stderr)
        miss = r.get("missing", {})
        print("  missing sample:", ", ".join(f"{nr}:{n}" for nr, n in list(miss.items())[:50]),
              file=sys.stderr)


if __name__ == "__main__":
    main()
