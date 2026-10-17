#!/usr/bin/env python3
"""
glcov-analyze.py — converts glcov probe results into the four-bucket coverage matrix.

Input: test-results/bridge-coverage-<tag>/glcov-results.json
       (from glcov-probe.mjs; maps handler name -> {err1, err2, us1, us2})

GL error codes: 0 = GL_NO_ERROR (working), non-zero = GL error (broken for
the given args, or stub). err == -1 means the proc address was missing
(not exported / not implemented).

Buckets:
  1. total tested
  2. fully working      (err==0, <SLOW_US, err1==err2)
  3. slow or flaky       (err==0 but >=SLOW_US, or err1!=err2)
  4. broken or stub      (err==-1 missing, or GL error != 0)

Slow threshold: 10ms (10,000 us), same justification as vk64 (METHOD.md).
"""
import json, sys, os

SLOW_US = 10_000

def bucket(name, r):
    err1, err2, us1, us2 = r['err1'], r['err2'], r['us1'], r['us2']
    worst_us = max(us1, us2)
    if err1 == -1 and err2 == -1:
        return ('stub', 'proc address missing: not exported/implemented')
    if err1 == -2 and err2 == -2:
        return ('crashed', 'guest crashed on this call (TRY line is the repro); skipped after recording')
    if err1 != err2:
        return ('flaky', f'err1={err1} err2={err2}')
    if err1 != 0:
        return ('broken', f'glGetError={err1}')
    if worst_us >= SLOW_US:
        return ('slow', f'{worst_us}us >= {SLOW_US}us')
    return ('working', f'{worst_us}us')

def main():
    if len(sys.argv) < 2:
        print("usage: glcov-analyze.py <glcov-results.json> [outdir]", file=sys.stderr)
        sys.exit(1)
    results = json.load(open(sys.argv[1]))
    outdir = sys.argv[2] if len(sys.argv) > 2 else '.'
    os.makedirs(outdir, exist_ok=True)

    rows = []
    for name in sorted(results):
        b, detail = bucket(name, results[name])
        r = results[name]
        rows.append((name, b, detail, r['err1'], r['err2'], r['us1'], r['us2']))

    counts = {}
    for _, b, _, _, _, _, _ in rows:
        counts[b] = counts.get(b, 0) + 1
    total = len(rows)
    working = counts.get('working', 0)
    slow_flaky = counts.get('slow', 0) + counts.get('flaky', 0)
    broken_stub = counts.get('broken', 0) + counts.get('stub', 0) + counts.get('crashed', 0)

    out = []
    out.append("# GL64 Bridge Coverage Matrix")
    out.append("")
    out.append(f"Source: glcov in-browser probe ({total} handlers tested).")
    out.append(f"Slow threshold: {SLOW_US}us (see METHOD.md).")
    out.append("")
    out.append("## Four-bucket summary")
    out.append("")
    out.append("| Bucket | Count |")
    out.append("|---|---|")
    out.append(f"| 1. Total handlers tested | {total} |")
    out.append(f"| 2. Fully working | {working} |")
    out.append(f"| 3. Slow or flaky | {slow_flaky} |")
    out.append(f"| 4. Broken or stub | {broken_stub} |")
    out.append("")
    out.append("## Per-handler table")
    out.append("")
    out.append("| Handler | Bucket | Detail | err1 | err2 | us1 | us2 |")
    out.append("|---|---|---|---|---|---|---|")
    for name, b, detail, e1, e2, us1, us2 in rows:
        out.append(f"| {name} | {b} | {detail} | {e1} | {e2} | {us1} | {us2} |")
    md = "\n".join(out) + "\n"
    with open(os.path.join(outdir, "matrix-gl64.md"), "w") as f:
        f.write(md)
    print(md)
    print(f"\n# buckets: total={total} working={working} slow_flaky={slow_flaky} broken_stub={broken_stub}",
          file=sys.stderr)

if __name__ == "__main__":
    main()
