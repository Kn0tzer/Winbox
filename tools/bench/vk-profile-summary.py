#!/usr/bin/env python3
"""Summarize repeated VKPROF runs; retain raw counts and sampling variance.

node probe output / console.txt inputs are supported. --warmup discards that
many observed frame records from each run. Legacy guest= is a wall residual,
not a CPU-interpreter measurement. Detailed exclusive times must conserve
root wall (allowing only the printed 0.001 ms rounding).
"""
import argparse
import json
import math
import re
import statistics
from pathlib import Path


def distribution(values):
    if not values:
        return None
    if not all(math.isfinite(v) for v in values):
        raise ValueError('non-finite profile measurement')
    return {'n': len(values), 'mean': statistics.mean(values),
            'sampleStddev': statistics.stdev(values) if len(values) > 1 else 0,
            'median': statistics.median(values), 'min': min(values), 'max': max(values)}


def summarize(text, warmup):
    frames = []
    detailed = {}
    for line in text.splitlines():
        match = re.search(r'vk64: VKPROF frame=(\d+) (.*)', line)
        if match:
            fields = dict((k, float(v)) for k, v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)ms', match[2]))
            fields['frame'] = int(match[1])
            for k, value in re.findall(r'(bufC|imgC)=(\d+)', match[2]):
                fields[k] = int(value)
            for k, hashed, sent in re.findall(r'(bufB|imgB)=(\d+)/(\d+)', match[2]):
                fields[k + 'Hashed'] = int(hashed)
                fields[k + 'Sent'] = int(sent)
            frames.append(fields)
        match = re.search(r'vk64: VKPROF2 frame=(\d+) (\{.*\})', line)
        if match:
            value = json.loads(match[2])
            difference = sum(s['exclusiveMs'] for s in value['stages'].values()) - value['rootMs']
            tolerance = len(value['stages']) * 0.0005 + 0.000002
            if abs(difference) > tolerance:
                raise ValueError(f'frame {match[1]} exclusive timing does not conserve root wall: {difference} ms')
            value['roundingDifferenceMs'] = difference
            detailed[int(match[1])] = value
    if not frames:
        raise ValueError('no VKPROF frame records; no measurement can be inferred')
    steady = frames[warmup:]
    if not steady:
        raise ValueError('no frame records remain after warmup')
    keys = sorted({k for f in steady for k in f if k != 'frame'})
    detail_frames = [detailed[f['frame']] for f in steady if f['frame'] in detailed]
    stages = {}
    for phase in sorted({p for d in detail_frames for p in d['stages']}):
        stages[phase] = {kind: distribution([d['stages'][phase][kind] for d in detail_frames if phase in d['stages']])
                         for kind in ('exclusiveMs', 'inclusiveMs', 'calls')}
    return {'observedFrames': len(frames), 'discardedWarmupFrames': warmup,
            'steadyFrames': len(steady), 'detailedFrames': len(detail_frames),
            'metrics': {k: distribution([f[k] for f in steady if k in f]) for k in keys},
            'stages': stages, 'frameIds': [f['frame'] for f in steady]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--warmup', type=int, default=8)
    parser.add_argument('logs', type=Path, nargs='+')
    args = parser.parse_args()
    if args.warmup < 0:
        parser.error('--warmup must be nonnegative')
    runs = [{'path': str(p), **summarize(p.read_text(), args.warmup)} for p in args.logs]
    metrics = sorted({k for run in runs for k in run['metrics']})
    print(json.dumps({'units': 'ms unless count/bytes',
                      'legacyGuestMeaning': 'unattributed wall residual; not an interpreter measurement',
                      'runs': runs,
                      'runMeanVariance': {k: distribution([r['metrics'][k]['mean'] for r in runs if k in r['metrics']]) for k in metrics}}, indent=2))


if __name__ == '__main__':
    main()
