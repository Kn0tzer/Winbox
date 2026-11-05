# Broad conformance censuses

These tools measure the implementation against the specification of the
surface it claims to support — enumerated completely, from the spec side —
instead of against the last program that crashed. A census turns "unknown
bugs" into a finite, countable list of unexamined entries, each of which
can then be dispositioned (implemented / loudly rejected / not-a-gap with
a reason) in the project's register style.

Neither census needs a browser, a guest boot, or a build. Both run in
seconds and produce JSON that a gate can diff.

## pe_census.py — the loader surface

Every Windows program starts by resolving its PE import table against the
DLLs present in the environment. If an imported (DLL, function) pair does
not resolve, the program cannot load — a fact that is fully decidable
statically, before any emulator runs.

The census parses the import tables (regular and delay-load, reported
separately) of every PE in each demo archive plus the runtime's own staged
files, and resolves them against the export tables of every PE the
runtime actually stages (the deployed runtime zips), chasing export
forwarders transitively. For archive programs it also computes the entry
executable's loader closure over the bundled files, splitting findings
into on-the-load-path vs. dead bundled files.

Reading the results:

- `MISSING-FUNC` / `MISSING-DLL` on a **regular, in-closure** import is a
  guaranteed load failure for that program as staged.
- The same on a **delay** import fails only when the code path runs.
- `APISET` imports resolve through Wine's api-set database, which the
  census does not itself validate; they are counted separately.
- Ordinal imports are counted, not name-checked.
- Resolution proves presence, not behaviour: a resolved function can
  still be a runtime stub. The census is a necessary-condition test.
- Dynamically loaded libraries (LoadLibrary, .NET P/Invoke) do not appear
  in import tables; bundled native libraries are censused as files in
  their own right, which covers the common cases.

Run:

```sh
python3 tools/conformance/pe_census.py \
  --runtime-dir /home/ubuntu/kn0tzer/winbox/runtime \
  --games-dir /home/ubuntu/kn0tzer/games \
  --out test-results/pe-census
```

## dll_export_census.py — the export-implementation surface

`pe_census.py` proves an imported function EXISTS in the staged
runtime. This census proves what the resolved export actually is.
Wine marks unimplemented exports in its spec files as `stub`, and its
build tool generates each one as the same tiny body that calls
`__wine_spec_unimplemented_stub` with the DLL's and function's own
names — so a stub is statically decidable, and self-validating: the
census counts a stub only when the name strings embedded in the body
match the export exactly (on the shipped set: 34,990 pattern hits, 0
unvalidated). Everything else with a body counts as provided (real
code, import thunks, win32u's syscall entries), forwarders count as
provided by their target, and data exports count as provided (they
are the implementation). Semi-stubs implemented in C with a plausible
return value are not statically visible; see the tool's docstring.

The censused input set is fixed: `wine64.zip` + `wine32.zip` (the
system libraries) plus the two prefix zips, whose override copies win
their (arch, DLL) pair in sorted order, as in `pe_census.py`. The
committed baseline (`dll_export_baseline.json`) is the census of the
currently shipped vintage, with the full per-function stub lists.

Run the census (with the demo cross-reference when a games dir is
given):

```sh
python3 tools/conformance/dll_export_census.py \
  --runtime-dir /home/ubuntu/kn0tzer/winbox/runtime \
  --games-dir /home/ubuntu/kn0tzer/games \
  --out test-results/dll-export-census
```

Check a tree against the baseline (0 = exact match, 1 = drift,
2 = different wine vintage):

```sh
python3 tools/conformance/dll_export_census.py \
  --runtime-dir web/runtime --check \
  --baseline tools/conformance/dll_export_baseline.json
```

The check also runs inside the JS unit suite
(`web/tests/suite/unit/dll-export-census.test.mjs`) whenever the
runtime zips are staged. A change that resolves a stub — or a
restage that changes the libraries — regenerates the baseline in
the same change:

```sh
python3 tools/conformance/dll_export_census.py \
  --runtime-dir web/runtime \
  --write-baseline tools/conformance/dll_export_baseline.json
```

First results (2026-11-05, shipped batch-7 vintage): 161,676 named
exports across 1,171 DLL tables; 127,920 (79.1%) real implementations
or forwarders; 33,756 (20.9%) validated unimplemented stubs, filed as
FD-167..FD-171. No demo program imports a stub on its static load
path. See `docs/reports/dll-export-census-20261105.md`.

## syscall_census.py — the kernel surface

The emulator's Linux kernel answers the syscalls Wine and glibc issue.
The census diffs the full syscall tables against the dispatch code:

- x86-64: the spec is the `x64SyscallName` table in
  `source/kernel/syscall64.cpp` (kept complete through Linux v6.12 for
  logging); the implementation is the set of `case X64_SYS_*:` labels in
  the dispatch switch in the same file.
- i386: the spec is the host's `unistd_32.h`; the implementation is the
  positional `syscallFunc[]` table in `source/kernel/syscall.cpp`
  (a `nullptr` slot returns `-ENOSYS`).

An undispatched syscall is not automatically a bug — much of the table is
legacy surface nothing calls. It is, precisely, the unexamined surface:
the list from which future "program X hangs/misbehaves" bugs will come.
Dispositioning it is register work, not guesswork.

Run:

```sh
python3 tools/conformance/syscall_census.py \
  --source-dir . \
  --unistd32 /usr/x86_64-linux-gnu/include/asm/unistd_32.h \
  --out test-results/syscall-census
```

## First results (2026-10-30)

See `docs/reports/broad-conformance-analysis-20261030.md`. Headlines:
the loader census caught a demo that cannot load at all before anyone
ran it (an incomplete user32 pointer-input family), and the syscall
census counted the unexamined kernel surface for the first time
(x86-64: 168 of 363 named syscalls undispatched; i386: 223 null slots
in the table plus 22 numbers beyond it).

## recover_gate_audit.py

Checks the logging invariant from
`docs/failure-logging-and-recovery.md`: the recovery switch
(`bw64RecoverEnabled()`) may gate recovery *actions*, never the
*reporting* of a fact. The audit scans the emulator sources for
`bw64RecoverLogOnce` reports inside a switch-gated `if` and fails
unless the same if/else reports in both branches (the loud-pair
shape) or the (file, key) pair carries a written exemption in the
script. Run from the repo root:

```sh
python3 tools/conformance/recover_gate_audit.py
```

Exit 0 means the invariant holds. A new gated report with a new
key fails the check until it is ungated, reshaped into a loud
pair, or exempted with a justification.
