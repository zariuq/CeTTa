#!/usr/bin/env python3
"""Compare qualified binaries on the PGO manifest's held-out inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import time

from cetta_pgo_train import workloads


def measure(binary, row, root, timeout, counters):
    command = [str(binary.resolve()), '--lang', row['language'], row['program']]
    if counters:
        command = ['perf', 'stat', '-x', ',', '-e', 'instructions:u,cycles:u,task-clock', '--', *command]
    start = time.monotonic()
    result = subprocess.run(command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)
    elapsed = time.monotonic() - start
    if result.returncode:
        raise ValueError(f'{binary}: {row["program"]}: status {result.returncode}: {result.stderr[-1500:]!r}')
    counts = {}
    if counters:
        for line in result.stderr.decode(errors='replace').splitlines():
            fields = line.split(',')
            if len(fields) < 3:
                continue
            try:
                value = float(fields[0])
            except ValueError:
                continue
            name = fields[2].split(':')[0]
            if name in ('instructions', 'cycles', 'task-clock') and value > 0:
                counts[name] = value
    return dict(output_sha256=hashlib.sha256(result.stdout).hexdigest(), wall_seconds=elapsed, **counts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--manifest', type=Path, default=Path('tools/cetta_pgo_workloads.json'))
    parser.add_argument('--binary', action='append', required=True, help='NAME=PATH; first is the reference')
    parser.add_argument('--repeats', type=int, default=5)
    parser.add_argument('--timeout', type=float, default=600)
    parser.add_argument('--wall-only', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.repeats < 1:
            parser.error('repeats must be positive')
        binaries = {}
        for value in args.binary:
            name, sep, path = value.partition('=')
            if not sep or not name or name in binaries:
                parser.error('each binary needs a unique NAME=PATH')
            binaries[name] = (args.root / path).resolve()
        if len(binaries) < 2:
            parser.error('compare at least two binaries')
        document = workloads(args.manifest, args.root)
        counters = not args.wall_only and shutil.which('perf') is not None
        names, rows = list(binaries), []
        for row in document['held_out']:
            samples = {name: [] for name in names}
            # Rotate the first binary to distribute warm-up and load drift.
            for repeat in range(args.repeats):
                for name in names[repeat % len(names):] + names[:repeat % len(names)]:
                    samples[name].append(measure(binaries[name], row, args.root, args.timeout, counters))
            outputs = {s['output_sha256'] for group in samples.values() for s in group}
            if len(outputs) != 1:
                raise ValueError(f'held-out output changed or was nondeterministic: {row["program"]}')
            summaries = {}
            for name, group in samples.items():
                available = set.intersection(*(set(s) for s in group)) - {'output_sha256'}
                summaries[name] = {key: statistics.median(s[key] for s in group) for key in sorted(available)}
                summaries[name]['wall_min_seconds'] = min(s['wall_seconds'] for s in group)
                summaries[name]['wall_max_seconds'] = max(s['wall_seconds'] for s in group)
            rows.append(dict(row, output_sha256=next(iter(outputs)), samples=samples, medians=summaries))
            print(f'held out: {row["language"]}: {row["program"]}: outputs agree; {summaries}', flush=True)
        receipt = dict(format='CettaPGOMeasurementsV1', repeats=args.repeats, counters_requested=counters,
                       binaries={name: dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                                 for name, path in binaries.items()},
                       manifest_sha256=hashlib.sha256(args.manifest.read_bytes()).hexdigest(), held_out=rows)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(receipt, indent=2) + '\n')
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))


if __name__ == '__main__':
    main()
