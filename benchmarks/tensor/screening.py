#!/usr/bin/env python3
"""Native resident screening sweep. Only aggregate receipts reach disk."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--records', nargs='+', type=int, default=[1000, 4000, 32000, 128000])
    parser.add_argument('--dimension', type=int, default=384)
    parser.add_argument('--repeats', type=int, default=8)
    parser.add_argument('--k', type=int, default=5)
    parser.add_argument('--seed', type=int, default=109107)
    parser.add_argument('--timeout', type=float, default=300)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if min(args.records) < args.k or min(args.dimension, args.repeats, args.k, args.seed) < 1 or args.timeout <= 0:
        parser.error('invalid sweep profile')
    paths = [ROOT/'src/tensor.c', ROOT/'src/tensor.h', Path(__file__).resolve(),
             ROOT/'benchmarks/tensor/screening.c']
    binary = args.binary.resolve()
    def hashes():
        return {'binary': hashlib.sha256(binary.read_bytes()).hexdigest(),
                **{str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
    before = hashes()
    receipt = dict(status='failed', programme_complete=False, seed=args.seed, sizes=[],
                   scope='resident native matvec plus selection; excludes service and interpreter',
                   retained_dataset_bytes=0)
    try:
        for n in args.records:
            result = subprocess.run([str(binary), str(n), str(args.dimension), str(args.repeats),
                str(args.k), str(args.seed)], check=True, text=True, capture_output=True, timeout=args.timeout)
            row = json.loads(result.stdout)
            row['p50'] = {field: statistics.median(s[field] for s in row['samples'])
                          for field in ('wide_ms', 'screen_ms', 'candidates')}
            receipt['sizes'].append(row)
            print(json.dumps({'rows': n, 'dimension': args.dimension, 'p50': row['p50']}), flush=True)
        receipt['status'] = 'passed'
    finally:
        receipt['hashes'] = before
        receipt['artifacts_unchanged'] = before == hashes()
        if not receipt['artifacts_unchanged']:
            receipt['status'] = 'failed'
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(receipt, indent=2)+'\n')
        if not receipt['artifacts_unchanged']:
            raise RuntimeError('qualification artifacts changed')


if __name__ == '__main__':
    main()
