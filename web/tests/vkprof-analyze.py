#!/usr/bin/env python3
"""
VKPROF Analysis Script
Parses per-frame VKPROF lines from console.log and computes
mean/p50/p95 per component to identify the D3D9 frame-time bottleneck.

VKPROF line format:
vk64: VKPROF frame=%llu wall=%.3fms submit=%.3fms bufdata=%.3fms bufB=%llu/%llu imgdata=%.3fms imgB=%llu/%llu trapOther=%.3fms(%llu calls) present=%.3fms hop=%.3fms hopB=%lluch/%lluB guest=%.3fms bufC=%llu imgC=%llu ser2=%.3fms queue=%.3fms topT=%s

Usage: python3 vkprof-analyze.py <console.log>
"""

import re
import sys
import statistics

# Regex to parse the 8 main components (all in ms) and topT
# Note: console.log lines are prefixed with "log: " by the probe's page.on('console') handler
VKPROF_RE = re.compile(
    r'VKPROF frame=(\d+)\s+'
    r'wall=([\d.]+)ms\s+'
    r'submit=([\d.]+)ms\s+'
    r'bufdata=([\d.]+)ms\s+'
    r'bufB=\d+/\d+\s+'
    r'imgdata=([\d.]+)ms\s+'
    r'imgB=\d+/\d+\s+'
    r'trapOther=([\d.]+)ms\(\d+ calls\)\s+'
    r'present=([\d.]+)ms\s+'
    r'hop=([\d.]+)ms\s+'
    r'hopB=\d+ch/\d+B\s+'
    r'guest=([\d.]+)ms\s+'
    r'bufC=\d+\s+imgC=\d+\s+'
    r'ser2=([\d.]+)ms\s+'
    r'queue=([\d.]+)ms\s+'
    r'topT=(.*)$'
)

COMPONENTS = ['wall', 'submit', 'bufdata', 'imgdata', 'trapOther', 'present', 'hop', 'guest']

def percentile(data, p):
    """Compute p-th percentile (0-100) using linear interpolation."""
    if not data:
        return 0.0
    s = sorted(data)
    k = (len(s) - 1) * (p / 100.0)
    f = int(k)
    c = f + 1
    if c >= len(s):
        return s[f]
    d0 = k - f
    return s[f] * (1 - d0) + s[c] * d0

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <console.log>", file=sys.stderr)
        sys.exit(1)

    path = sys.argv[1]
    frames = []  # list of dicts: {component: ms, ...}
    topT_samples = []

    try:
        with open(path, 'r', errors='replace') as f:
            for line in f:
                m = VKPROF_RE.search(line)
                if not m:
                    continue
                vals = {
                    'wall': float(m.group(2)),
                    'submit': float(m.group(3)),
                    'bufdata': float(m.group(4)),
                    'imgdata': float(m.group(5)),
                    'trapOther': float(m.group(6)),
                    'present': float(m.group(7)),
                    'hop': float(m.group(8)),
                    'guest': float(m.group(9)),
                }
                frames.append(vals)
                topT = m.group(12).strip()
                if topT:
                    topT_samples.append(topT)
    except FileNotFoundError:
        print(f"Error: file not found: {path}", file=sys.stderr)
        sys.exit(1)

    n = len(frames)
    print(f"VKPROF frames parsed: {n}")
    if n == 0:
        print("No VKPROF lines found. Was ?vkprof=1 in the URL?")
        sys.exit(1)

    # Compute stats per component
    print()
    print(f"{'component':<12} {'mean_ms':>10} {'p50_ms':>10} {'p95_ms':>10} {'%_of_wall':>10}")
    print("-" * 56)
    wall_mean = statistics.mean(f['wall'] for f in frames)
    rows = []
    for comp in COMPONENTS:
        vals = [f[comp] for f in frames]
        mean = statistics.mean(vals)
        p50 = percentile(vals, 50)
        p95 = percentile(vals, 95)
        pct = (mean / wall_mean * 100.0) if wall_mean else 0.0
        rows.append((comp, mean, p50, p95, pct))
    # Sort by mean descending to show the dominant component first
    rows.sort(key=lambda r: r[1], reverse=True)
    for comp, mean, p50, p95, pct in rows:
        print(f"{comp:<12} {mean:>10.3f} {p50:>10.3f} {p95:>10.3f} {pct:>9.1f}%")
    print("-" * 56)
    print(f"{'wall_total':<12} {wall_mean:>10.3f}  (reference: sum of component means)")

    # Sanity: sum of non-wall components vs wall
    comp_sum = sum(r[1] for r in rows if r[0] != 'wall')
    print(f"sum(others)  {comp_sum:>10.3f}  (wall - guest_overlap note: guest=wall-submit-trapOther-present)")

    # Top trapOther contributors (last few samples)
    if topT_samples:
        print()
        print("topT samples (top-3 trapOther contributors by wall time):")
        for t in topT_samples[-5:]:
            print(f"  {t}")

    # Dominant component verdict (exclude 'wall' — it's the total, not a sub-component)
    non_wall = [r for r in rows if r[0] != 'wall']
    dom = non_wall[0] if non_wall else rows[0]
    print()
    print(f"Dominant component (excl. wall): {dom[0]} ({dom[1]:.3f}ms mean, {dom[4]:.1f}% of wall)")

if __name__ == '__main__':
    main()
