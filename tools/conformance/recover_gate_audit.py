#!/usr/bin/env python3
"""recover_gate_audit.py - reporting must not sit behind the recovery switch.

Invariant (docs/failure-logging-and-recovery.md, lane-cleanlog): the
recovery switch bw64RecoverEnabled() may gate recovery *actions*, but
never the *reporting* of a fact. A bw64RecoverLogOnce report is
therefore a violation when it appears inside an if whose condition
mentions bw64RecoverEnabled(), unless one of these holds:

- the same if/else reports in the other branch too (the loud pair
  shape used by the fcntl / ioctl / setsockopt / signalfd4 sites), or
- the (file, key) pair is listed in EXEMPTIONS below, each with its
  justification. Exemptions are the audit trail: a new gated report
  with a new key fails this check.

What this does not flag, deliberately: blocks whose contents are
recovery actions (instruction skip in cpu64 / normal_other.h) - they
contain no bw64RecoverLogOnce call; the failures themselves are
reported on the unconditional fall-through paths.

Usage: recover_gate_audit.py [--source-dir DIR]   (default ./source)
Exit 0 = invariant holds; 1 = at least one violation.
"""
import os
import re
import sys

# (file name, LogOnce key) -> why this gated report is tolerated.
EXEMPTIONS = {}
# The 11 panic-default syscall sites: grace existed only behind the
# switch, kpanic was the default. Lane-cleanlog commit 648241602
# already converts exactly these (grace unconditional, panic
# deleted), so this lane does not duplicate them and the merges stay
# composable; once that lands, these exemptions should be removed.
for _f, _k in [
    ("ksystem.cpp", "syscall.ugetrlimit.resource"),
    ("ksystem.cpp", "syscall.clock_gettime.clock"),
    ("ksystem.cpp", "syscall.clock_gettime64.clock"),
    ("ksystem.cpp", "syscall.prlimit64.resource"),
    ("kthread.cpp", "syscall.modify_ldt.unknown"),
    ("kthread.cpp", "syscall.clock_nanosleep.flags"),
    ("kthread.cpp", "syscall.sigprocmask.how"),
    ("kthread.cpp", "syscall.sigtimedwait.sigsetsize"),
    ("syscall.cpp", "syscall.socketcall.unknown"),
    ("syscall.cpp", "syscall.ipc.unknown"),
    ("syscall.cpp", "syscall.vm86"),
]:
    EXEMPTIONS[(_f, _k)] = "panic-default site converted by lane-cleanlog 648241602"
EXEMPTIONS[("kthread.cpp", "syscall.futex.unknown")] = (
    "loud overall: with the switch off, an unsupported futex op "
    "reaches the dispatch tail, which kwarns with the op value; the "
    "gate controls only the early ENOSYS reject")
EXEMPTIONS[("knetlink.cpp", "socket.netlink.setsockopt")] = (
    "loud in both modes: with the switch off, the next statement "
    "kwarns with the value; the gate controls only the EINVAL return")
EXEMPTIONS[("knetlink.cpp", "socket.netlink.setsockopt.level")] = (
    "loud in both modes: with the switch off, the next statement "
    "kwarns with the value; the gate controls only the EINVAL return")

REPORT_TOKEN = re.compile(r"kwarn|klog|bw64RecoverLogOnce")
LOG_ONCE = re.compile(r"bw64RecoverLogOnce\s*\(")
IF_RE = re.compile(r"\bif\s*\(")


def strip_code(text):
    """Blank string/char literals and comments, preserving offsets."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n - 2 if j < 0 else j
            for k in range(i, min(j + 2, n)):
                if text[k] != "\n":
                    out[k] = " "
            i = j + 2
        elif c in "\"'":
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == c or text[j] == "\n":
                    break
                j += 1
            for k in range(i, min(j + 1, n)):
                if text[k] != "\n":
                    out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def match_delim(text, i, open_c, close_c):
    depth = 0
    while i < len(text):
        if text[i] == open_c:
            depth += 1
        elif text[i] == close_c:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def body_span(text, i):
    """Span of an if/else body starting at or after offset i."""
    while i < len(text) and text[i] in " \t\r\n":
        i += 1
    if i < len(text) and text[i] == "{":
        j = match_delim(text, i, "{", "}")
        return (i, j + 1) if j >= 0 else (i, len(text))
    j = text.find(";", i)
    return (i, j + 1) if j >= 0 else (i, len(text))


def audit_file(path):
    raw = open(path, encoding="utf-8", errors="replace").read()
    code = strip_code(raw)
    name = os.path.basename(path)
    violations, tolerated = [], []
    for m in IF_RE.finditer(code):
        open_paren = code.index("(", m.start())
        close_paren = match_delim(code, open_paren, "(", ")")
        if close_paren < 0:
            continue
        condition = code[open_paren + 1:close_paren]
        if "bw64RecoverEnabled" not in condition:
            continue
        b0, b1 = body_span(code, close_paren + 1)
        # An else branch that also reports makes the pair loud.
        j = b1
        while j < len(code) and code[j] in " \t\r\n":
            j += 1
        else_reports = False
        if code[j:j + 4] == "else" and (
                j + 4 >= len(code)
                or not (code[j + 4].isalnum() or code[j + 4] == "_")):
            e0, e1 = body_span(code, j + 4)
            else_reports = REPORT_TOKEN.search(code[e0:e1]) is not None
        for call in LOG_ONCE.finditer(code, b0, b1):
            km = re.search(r'"([^"]*)"', raw[call.end():call.end() + 200])
            key = km.group(1) if km else "<unknown>"
            line = raw.count("\n", 0, call.start()) + 1
            if else_reports:
                tolerated.append((line, key, "loud if/else pair"))
            elif (name, key) in EXEMPTIONS:
                tolerated.append((line, key, EXEMPTIONS[(name, key)]))
            else:
                violations.append((line, key))
    return violations, tolerated


def main():
    args = sys.argv[1:]
    source_dir = "source"
    if "--source-dir" in args:
        source_dir = args[args.index("--source-dir") + 1]
    total_v, total_t = [], 0
    for root, _dirs, files in os.walk(source_dir):
        for f in sorted(files):
            if not f.endswith((".cpp", ".h", ".hpp", ".inl")):
                continue
            path = os.path.join(root, f)
            if os.path.basename(path) == "log.cpp":
                continue  # the helper's own home; the gate lives there
            v, t = audit_file(path)
            total_t += len(t)
            for line, key in v:
                total_v.append("%s:%d: gated report %r" % (path, line, key))
    for v in total_v:
        print("VIOLATION", v)
    print("recover-gate audit: %d gated report(s) tolerated with "
          "justification, %d violation(s)" % (total_t, len(total_v)))
    return 1 if total_v else 0


if __name__ == "__main__":
    sys.exit(main())
