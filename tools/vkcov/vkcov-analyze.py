#!/usr/bin/env python3
"""
vkcov-analyze.py — converts vkcov probe results into the four-bucket coverage matrix.

Input: test-results/bridge-coverage-<tag>/vkcov-results.json
       (from vkcov-probe.mjs; maps handler name -> {rc1, rc2, us1, us2})

Buckets per API set:
  1. total tested
  2. fully working      (rc==0, <SLOW_US, rc1==rc2)
  3. slow or flaky       (rc==0 but >=SLOW_US, or rc1!=rc2)
  4. broken or stub      (rc==-8 stub, other errors, or missing=crashed)

Slow threshold: 10ms (10,000 us). Justification (METHOD.md): the guest-CPU
profile measured 48 vk64 traps at ~34.5ms total (~0.72ms avg per trap). A
simple marshalled call doing no GPU work has no business taking 10x+ that.

Output: Markdown table + bucket summary to stdout; also writes
        test-results/bridge-coverage/matrix-vk64.md
"""
import json, sys, os

SLOW_US = 10_000
STUB_RC = -8  # VK_ERROR_FEATURE_NOT_PRESENT

def bucket(name, r):
    rc1, rc2, us1, us2 = r['rc1'], r['rc2'], r['us1'], r['us2']
    worst_us = max(us1, us2)
    if rc1 != rc2:
        return ('flaky', f'rc1={rc1} rc2={rc2}')
    rc = rc1
    if rc == STUB_RC:
        return ('stub', 'VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub')
    if rc == -2:
        return ('crashed', 'guest crashed on this call (TRY line is the repro); skipped after recording')
    if rc != 0:
        return ('broken', f'rc={rc}')
    if worst_us >= SLOW_US:
        return ('slow', f'{worst_us}us >= {SLOW_US}us')
    return ('working', f'{worst_us}us')

def main():
    if len(sys.argv) < 2:
        print("usage: vkcov-analyze.py <vkcov-results.json> [outdir]", file=sys.stderr)
        sys.exit(1)
    results = json.load(open(sys.argv[1]))
    outdir = sys.argv[2] if len(sys.argv) > 2 else '.'
    os.makedirs(outdir, exist_ok=True)

    rows = []
    for name in sorted(results):
        b, detail = bucket(name, results[name])
        r = results[name]
        rows.append((name, b, detail, r['rc1'], r['rc2'], r['us1'], r['us2']))

    counts = {}
    for _, b, _, _, _, _, _ in rows:
        counts[b] = counts.get(b, 0) + 1
    total = len(rows)
    working = counts.get('working', 0)
    slow_flaky = counts.get('slow', 0) + counts.get('flaky', 0)
    broken_stub = counts.get('broken', 0) + counts.get('stub', 0) + counts.get('crashed', 0)

    out = []
    out.append("# vk64 Bridge Coverage Matrix")
    out.append("")
    out.append(f"Source: vkcov in-browser probe ({total} handlers tested).")
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
    out.append("| Handler | Bucket | Detail | rc1 | rc2 | us1 | us2 |")
    out.append("|---|---|---|---|---|---|---|")
    for name, b, detail, rc1, rc2, us1, us2 in rows:
        out.append(f"| {name} | {b} | {detail} | {rc1} | {rc2} | {us1} | {us2} |")
    md = "\n".join(out) + "\n"
    with open(os.path.join(outdir, "matrix-vk64.md"), "w") as f:
        f.write(md)
    print(md)
    print(f"\n# buckets: total={total} working={working} slow_flaky={slow_flaky} broken_stub={broken_stub}",
          file=sys.stderr)

if __name__ == "__main__":
    main()
