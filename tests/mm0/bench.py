#!/usr/bin/env python3
"""Time, peak memory and heap allocations of the native verifier and mm0-c
on the same MMB files.

    python3 tests/mm0/bench.py --native NATIVE --mm0c MM0C --measure MEASURE
        [--shim alloc_count.so] [--runs N] [--json OUT] FILE.mmb:FILE.mm0 ...

Every run is one process, started by the small launcher tests/mm0/measure.c
and measured by it from fork to exit: wall time, and the peak resident set
from wait4 (a child started from this interpreter would be charged the
interpreter's own size).  Runs of the verifiers alternate, so drift in
the machine's speed falls on all of them alike.  The native verifier runs
three ways: retaining the checked theory (its default), --check-only
(retaining nothing, like mm0-c), and --check-only --mmap (also mapping the
MMB file, as mm0-c does, instead of reading it into memory).  With --shim,
one more run of each counts heap allocations and the bytes they request.

Verdicts are compared, never timed alone: a run that fails is reported.
"""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile


MEASURE = None


def spawn(cmd, stdin_path, env=None):
    """Run cmd with stdin from stdin_path; return (status, seconds, maxrss KiB)."""
    out = subprocess.run([MEASURE, stdin_path] + cmd, capture_output=True, text=True, env=env, check=True)
    status, ns, maxrss = (int(x) for x in out.stdout.split())
    return status, ns / 1e9, maxrss


def allocations(cmd, stdin_path, shim):
    """Heap allocations of one run (started directly: the launcher would
    count its own)."""
    with tempfile.NamedTemporaryFile(mode="r", suffix=".count") as out, open(stdin_path, "rb") as stdin:
        env = dict(os.environ, LD_PRELOAD=shim, MM0_ALLOC_COUNT_FILE=out.name)
        status = subprocess.run(cmd, stdin=stdin, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                env=env).returncode
        fields = Path(out.name).read_text().split()
    calls, requested, frees = (int(x) for x in fields) if len(fields) == 3 else (None, None, None)
    return status, calls, requested, frees


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--native", required=True)
    ap.add_argument("--mm0c", required=True)
    ap.add_argument("--measure", required=True, help="the launcher built from tests/mm0/measure.c")
    ap.add_argument("--shim")
    ap.add_argument("--runs", type=int, default=20)
    ap.add_argument("--json")
    ap.add_argument("pairs", nargs="+", help="FILE.mmb:FILE.mm0")
    args = ap.parse_args()
    global MEASURE
    MEASURE = str(Path(args.measure).resolve())
    native, mm0c = str(Path(args.native).resolve()), str(Path(args.mm0c).resolve())
    variants = [
        ("mm0-c", lambda mmb: [mm0c, mmb]),
        ("native", lambda mmb: [native, mmb]),
        ("native --check-only", lambda mmb: [native, "--check-only", mmb]),
        ("native --check-only --mmap", lambda mmb: [native, "--check-only", "--mmap", mmb]),
    ]
    report = []
    for pair in args.pairs:
        mmb, spec = pair.split(":")
        name = Path(mmb).stem
        times = {v: [] for v, _ in variants}
        rss = {v: 0 for v, _ in variants}
        for _ in range(args.runs):
            for variant, cmd in variants:
                status, elapsed, maxrss = spawn(cmd(mmb), spec)
                if status != 0:
                    sys.exit(f"{variant} fails on {name} with status {status}")
                times[variant].append(elapsed)
                rss[variant] = max(rss[variant], maxrss)
        retained = subprocess.run([native, mmb], stdin=open(spec, "rb"), capture_output=True, text=True).stdout.strip()
        for variant, cmd in variants:
            row = {"file": name, "verifier": variant, "runs": args.runs,
                   "min_ms": 1000 * min(times[variant]), "median_ms": 1000 * statistics.median(times[variant]),
                   "maxrss_kib": rss[variant]}
            if args.shim:
                status, calls, requested, frees = allocations(cmd(mmb), spec, str(Path(args.shim).resolve()))
                row.update(allocations=calls, requested_bytes=requested, frees=frees)
            report.append(row)
        print(f"{name}: {Path(mmb).stat().st_size} bytes; native {retained}")
        for row in report[-len(variants):]:
            line = (f"  {row['verifier']:28} min {row['min_ms']:8.2f} ms  median {row['median_ms']:8.2f} ms  "
                    f"peak RSS {row['maxrss_kib'] / 1024:7.1f} MiB")
            if args.shim:
                line += f"  allocations {row['allocations']:>7}  requested {row['requested_bytes'] / 1048576:8.1f} MiB"
            print(line, flush=True)
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
