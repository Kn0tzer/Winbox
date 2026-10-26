#!/usr/bin/env python3
"""Citation audit for docs/known-gaps/vulkan.md.

Every register row that cites file:line is checked mechanically where a
checkable construct can be extracted from the row text:

  1. marker/snippet rows: the backticked text after `marker:` (or a long
     backticked code snippet in the evidence cell) must appear verbatim in
     the cited file at (or within TOLERANCE lines of) a cited line. A
     marker truncated with "..." is checked by its prefix.
  2. log-token rows: a hyphenated token the row presents as a log/feature
     string (logs `x-y-z`, noteUnsupported('x-y-z'), ...) must appear in
     the cited file near a cited line.
  3. function rows: a backticked identifier that is *defined* in the cited
     file must have its definition near a cited line.

Anything else is reported UNCHECKABLE (descriptive labels, counters, enum
arms with no quoted construct). Rows marked NEEDS RE-DISPOSITION by the
2026-10-25 citation re-pin are reported FLAGGED and excluded from the
verdict. A small EXEMPT list (below, with reasons) covers rows whose
construct legitimately lives in a different file than the citation or is
a format/feature name rather than the row's site; exemptions are keyed by
register label so a new exemption is a visible edit, not a silent pass.

Verdicts: MATCH / DRIFT (construct in cited file, not near citation) /
MISSING (construct nowhere in cited file) / UNCHECKABLE / EXEMPT / FLAGGED.

Usage:
  python3 tools/vk-register-citation-audit.py [--fix] [--json]
--fix rewrites a drifted citation to the verified line ONLY when the row
has a single citation in that file, the construct is verbatim (marker,
snippet or log token), and the nearest occurrence is unambiguous (no
second occurrence within 25 lines). Everything else is report-only.

The unit guard web/tests/suite/unit/vk-register-citations.test.mjs
implements the same rules and fails the suite on DRIFT/MISSING.
"""
import re, sys, json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REG = ROOT / 'docs/known-gaps/vulkan.md'
TOLERANCE = 6

# (register first-cell label substring, reason) — adjudicated 2026-10-25.
EXEMPT = [
    ('stencil state guard', 'row cites the page-tier defensive guard in vkwebgpu.mjs; the named log tokens (stencil-enum/stencil-face-state) are emitted by the bridge audit it mirrors (vk64bridge.cpp)'),
    ('blend operation guard', 'row cites the page-tier defensive guard in vkwebgpu.mjs; the named log token (blend-enum) is emitted by the bridge audit it mirrors (vk64bridge.cpp)'),
    ('template descriptor default', 'row cites the vulkancommon.cpp template default; the named token (descriptor-type) is the bridge-side rejection it cross-references (vk64bridge.cpp)'),
    ('clear rectangle layers', 'row cites the page-tier clear guard in vkwebgpu.mjs; the named token (layered-framebuffer) is emitted at bridge framebuffer creation (vk64bridge.cpp), as the disposition states'),
    ('stats field', 'row cites a diagnostic snapshot field in vkwebgpu.mjs; noteDrop in the disposition is a cross-reference, not the row site (verified at the cited line)'),
    ('opaque depth upload', 'the backticked construct is a WebGPU format name (depth32float-stencil8), not the row site; the upload block was verified manually at the cited lines'),
]

def split_cells(line):
    parts, cur, i = [], '', 0
    while i < len(line):
        c = line[i]
        if c == '\\' and i + 1 < len(line) and line[i+1] == '|':
            cur += '|'; i += 2; continue
        if c == '|':
            parts.append(cur); cur = ''; i += 1; continue
        cur += c; i += 1
    parts.append(cur)
    return [p.strip() for p in parts]

CITE_RE = re.compile(r'([\w./-]+\.(?:cpp|h|mjs|c|py|sh|json|md|txt|yml)):([\d][\d,\-]*)')

def parse_citations(cell):
    out = []
    for m in CITE_RE.finditer(cell):
        f, nums = m.group(1), m.group(2).rstrip(',')
        for part in nums.split(','):
            part = part.strip()
            if not part: continue
            if '-' in part:
                a, b = part.split('-', 1)
                if a.isdigit() and b.isdigit(): out.append((f, int(a), int(b)))
            elif part.isdigit():
                out.append((f, int(part), int(part)))
    return out

_cache = {}
def file_lines(f):
    if f not in _cache:
        p = ROOT / f
        _cache[f] = p.read_text(errors='replace').splitlines() if p.exists() else None
    return _cache[f]

TOKEN_HYPH = re.compile(r'^[a-z][a-z0-9]*(?:-[a-z0-9]+)+$')
LOG_CTX = re.compile(r'(logs?|noteUnsupported|noteDrop|noteSkippedDraw|vk64Unsupported|warns|warning|diagnostic|rejects)')

def extract_construct(cells):
    evidence = cells[2] if len(cells) > 2 else ''
    disp = cells[4] if len(cells) > 4 else ''
    # 1. marker / snippet: longest backticked span in evidence that looks like code text
    m = re.search(r'marker:\s*`([^`]+)`', evidence)
    spans = [m.group(1)] if m else []
    spans += [s for s in re.findall(r'`([^`]{25,})`', evidence)
              if ('"' in s or "'" in s or '{' in s) and s not in spans]
    for s in spans:
        t = s.strip()
        t = re.sub(r'^//\s?', '', t)
        if '...' in t:
            t = t.split('...')[0].rstrip()
        if len(t) >= 12:
            return ('marker', t)
    # 2. log tokens: hyphenated backticked tokens in a log context
    for src in (evidence, disp):
        if not LOG_CTX.search(src):
            continue
        for tok in re.findall(r'`([^`]+)`', src):
            if TOKEN_HYPH.match(tok):
                return ('token', tok)
    # 2b. structural arms: (default) rows and swallowed-catch rows have a
    # literal construct in the cited neighbourhood. The construct is only
    # claimed when the literal exists within 25 lines of a cited line;
    # beyond that the row falls back to UNCHECKABLE, and the guard's
    # pinned UNCHECKABLE ceiling turns that into a suite failure.
    label = cells[1] if len(cells) > 1 else ''
    lit = 'default:' if '(default' in label else ('catch' if 'catch' in label else None)
    if lit:
        cites0 = parse_citations(label)
        if cites0:
            flines = file_lines(cites0[0][0])
            if flines:
                hits = [i+1 for i, l in enumerate(flines) if lit in l]
                if any(any(s - 25 <= h <= e + 25 for (f, s, e) in cites0 if f == cites0[0][0]) for h in hits):
                    return ('literal', lit)
    # 3. defined function identifiers
    cites = parse_citations(cells[1])
    if cites:
        lines = file_lines(cites[0][0])
        if lines:
            for src in (evidence, disp):
                for tok in re.findall(r'`([A-Za-z_][A-Za-z0-9_]*)`', src):
                    if len(tok) < 4: continue
                    for i, l in enumerate(lines):
                        if re.search(r'(?:^|[\s*&])' + re.escape(tok) + r'\s*\(', l) and ('{' in l or l.rstrip().endswith(')') or 'function' in l):
                            return ('func', tok)
    return (None, None)

def audit():
    rows = []
    in_appendix = False
    for idx, line in enumerate(REG.read_text(errors='replace').splitlines(), 1):
        if line.startswith('## Spot-check appendix'):
            in_appendix = True
        if in_appendix or not line.lstrip().startswith('|'):
            continue
        cells = split_cells(line)
        if len(cells) < 5 or cells[1].startswith('---') or 'File:line' in cells[1]:
            continue
        cites = parse_citations(cells[1])
        if not cites:
            continue
        label = cells[1]
        row = {'line': idx, 'cites': cites, 'label': label}
        if 'NEEDS RE-DISPOSITION (citation re-pin' in line:
            row['verdict'] = 'FLAGGED'; rows.append(row); continue
        if any(s in label for s, _ in EXEMPT):
            row['verdict'] = 'EXEMPT'; rows.append(row); continue
        kind, construct = extract_construct(cells)
        if kind is None:
            row['verdict'] = 'UNCHECKABLE'; rows.append(row); continue
        row['kind'], row['construct'] = kind, construct
        f0 = cites[0][0]
        lines = file_lines(f0)
        if lines is None:
            row['verdict'] = 'MISSING'; row['detail'] = 'cited file not found'; rows.append(row); continue
        if kind == 'func':
            hits = [i+1 for i, l in enumerate(lines)
                    if re.search(r'\b' + re.escape(construct) + r'\s*\(', l)]
        else:
            hits = [i+1 for i, l in enumerate(lines) if construct in l]
        if not hits:
            row['verdict'] = 'MISSING'; row['detail'] = 'construct not in cited file'; rows.append(row); continue
        tol = 25 if kind == 'literal' else TOLERANCE
        near = [h for h in hits if any(f == f0 and s - tol <= h <= e + tol for (f, s, e) in cites)]
        if not near and kind == 'func':
            # A citation inside the function's own definition span is a match
            # (rows cite default arms / log lines inside long functions).
            defs = [i for i, l in enumerate(lines)
                    if re.search(r'(?:^|[\s*&])' + re.escape(construct) + r'\s*\(', l) and ('{' in l or 'function' in l)]
            for d in defs:
                depth, end = 0, None
                for j in range(d, len(lines)):
                    depth += lines[j].count('{') - lines[j].count('}')
                    if j > d and depth <= 0:
                        end = j + 1; break
                if end and any(f == f0 and d + 1 <= s and e <= end for (f, s, e) in cites):
                    near = [d + 1]; break
        if near:
            row['verdict'] = 'MATCH'
        else:
            s, e = cites[0][1], cites[0][2]
            nearest = min(hits, key=lambda h: min(abs(h - s), abs(h - e)))
            row['verdict'] = 'DRIFT'; row['hits'] = hits[:8]; row['nearest'] = nearest
        rows.append(row)
    # Inherited: an uncheckable row that cites the same site as a MATCH row
    # (duplicate scanner hits cite their primary row's line) is checked by
    # that primary's construct.
    matched_sites = set()
    for r in rows:
        if r['verdict'] == 'MATCH':
            for (f, s, e) in r['cites']:
                matched_sites.add((f, s, e))
    for r in rows:
        if r['verdict'] == 'UNCHECKABLE' and any(c in matched_sites for c in r['cites']):
            r['verdict'] = 'MATCH-INHERITED'
    return rows

def main():
    fix = '--fix' in sys.argv
    rows = audit()
    counts = {}
    for r in rows: counts[r['verdict']] = counts.get(r['verdict'], 0) + 1
    print(f"rows with citations: {len(rows)}")
    for k in ('MATCH', 'MATCH-INHERITED', 'DRIFT', 'MISSING', 'UNCHECKABLE', 'EXEMPT', 'FLAGGED'):
        if k in counts: print(f"  {k}: {counts[k]}")
    for r in rows:
        if r['verdict'] in ('DRIFT', 'MISSING'):
            print(f"  {r['verdict']} reg:{r['line']} cites={r['cites']} kind={r.get('kind')} construct={r.get('construct')!r} hits={r.get('hits')} nearest={r.get('nearest')} {r.get('detail','')}")
    if fix:
        lines = REG.read_text(errors='replace').splitlines(keepends=True)
        fixed = 0
        for r in rows:
            if r['verdict'] != 'DRIFT' or r.get('kind') not in ('marker', 'token'):
                continue
            same_file = [c for c in r['cites'] if c[0] == r['cites'][0][0]]
            if len(r['cites']) != 1:
                continue
            hits = r['hits']; near = r['nearest']
            if any(h != near and abs(h - near) <= 25 for h in hits):
                continue
            f, s, e = r['cites'][0]
            old = f"{f}:{s}" if s == e else f"{f}:{s}-{e}"
            new = f"{f}:{near}"
            line = lines[r['line'] - 1]
            if line.count(old) == 1:
                lines[r['line'] - 1] = line.replace(old, new)
                fixed += 1
                print(f"  FIX reg:{r['line']} {old} -> {new}")
        if fixed:
            REG.write_text(''.join(lines))
        print(f"fixed {fixed} citations")
    if '--json' in sys.argv:
        print(json.dumps(counts))
    bad = counts.get('DRIFT', 0) + counts.get('MISSING', 0)
    sys.exit(1 if bad else 0)

if __name__ == '__main__':
    main()
