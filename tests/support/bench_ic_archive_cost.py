#!/usr/bin/env python3
"""Paired archive-check cost experiment; explicit, not an ordinary test sweep."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics
import subprocess
import time

from check_ic_archive import ROOT, encode, syntax
from check_ic_search import fixtures


# Previous encode-before-comparison implementation, retained only as a control.
BASELINE = r'''
(= (archive-check-encoded $original $current $candidate)
   (case ((ic-archive:prepare $original) (ic-archive:prepare $current) (ic-archive:prepare $candidate))
     ((((IC:PreparedArchive $original-tree $catalog $source)
        (IC:PreparedArchive $before-tree $before-catalog $before-source)
        (IC:PreparedArchive $after-tree $after-catalog $after-source))
       (if (and (== $catalog $before-catalog) (== $catalog $after-catalog))
         (if (and (== $source $before-source) (== $source $after-source))
             (case ((binary-tree:encode $before-tree) (binary-tree:encode $after-tree))
               ((((Binary:TreeBytes $before) (Binary:TreeBytes $after))
                 (case ((tensor:shape $before) (tensor:shape $after))
                   (((($before-size) ($after-size))
                     (if (< $after-size $before-size)
                       (IC:ArchiveAccepted $candidate $after $before-size $after-size)
                       (IC:ArchiveRejected NotSmaller))))))
                ($other (IC:ArchiveRejected InvalidEncoding))))
             (IC:ArchiveRejected Reconstruction))
         (IC:ArchiveRejected Catalog)))
      ($other (IC:ArchiveRejected InvalidArchive)))))
'''

MEASURED = r'''
(= (archive-check-sized $original $current $candidate)
   (case ((ic-archive:prepare $original) (ic-archive:prepare $current) (ic-archive:prepare $candidate))
     ((((IC:PreparedArchive $original-tree $catalog $source)
        (IC:PreparedArchive $before-tree $before-catalog $before-source)
        (IC:PreparedArchive $after-tree $after-catalog $after-source))
       (if (and (== $catalog $before-catalog) (== $catalog $after-catalog))
         (if (and (== $source $before-source) (== $source $after-source))
           (let* (($before-size (binary-tree:byte-length $before-tree))
                  ($after-size (binary-tree:byte-length $after-tree)))
             (if (and (binary:unsigned? $before-size) (binary:unsigned? $after-size))
               (if (< $after-size $before-size)
                 (case (binary-tree:encode $after-tree)
                   (((Binary:TreeBytes $after) (IC:ArchiveAccepted $candidate $after $before-size $after-size))
                    ($other (IC:ArchiveRejected InvalidEncoding))))
                 (IC:ArchiveRejected NotSmaller))
               (IC:ArchiveRejected InvalidEncoding)))
           (IC:ArchiveRejected Reconstruction))
         (IC:ArchiveRejected Catalog)))
      ($other (IC:ArchiveRejected InvalidArchive)))))
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--samples", type=int, default=3)
    ap.add_argument("--iterations", type=int, default=32)
    args = ap.parse_args()
    if args.samples < 1 or args.iterations < 1:
        ap.error("samples and iterations must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    original, improved = fixtures()
    samples = {outcome: {mode: [] for mode in ("encoded", "measured")} for outcome in ("accept", "refuse")}
    for outcome in samples:
        candidate = improved if outcome == "accept" else original
        expected = args.iterations * (len(encode(original)) + len(encode(candidate))) if outcome == "accept" else 0
        for mode, function in (("encoded", "archive-check-encoded"), ("measured", "archive-check-sized")):
            program = '!(import! &self "lib/petta/lib_ic_archive.metta")\n' + BASELINE + MEASURED + f'''
(= (cost-repeat $n $original $candidate)
   (if (== $n 0) 0
     (let $result ({function} $original $original $candidate)
       (case $result
         (((IC:ArchiveAccepted $archive $bytes $before $after)
           (+ (+ $before $after) (cost-repeat (- $n 1) $original $candidate)))
          ((IC:ArchiveRejected NotSmaller) (cost-repeat (- $n 1) $original $candidate)))))))
!(println! (CostObservation (cost-repeat {args.iterations} {syntax(original)} {syntax(candidate)})))
'''
            (args.output / f"{outcome}-{mode}.metta").write_text(program)
        for sample in range(args.samples):
            order = ("encoded", "measured") if sample % 2 == 0 else ("measured", "encoded")
            for mode in order:
                path = args.output / f"{outcome}-{mode}.metta"
                start = time.perf_counter()
                run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(path.resolve())],
                                     cwd=ROOT, text=True, capture_output=True, timeout=60)
                elapsed = time.perf_counter() - start
                (args.output / f"{outcome}-{mode}-{sample}.log").write_text(run.stdout + run.stderr)
                if run.returncode or f"(CostObservation {expected})" not in run.stdout:
                    raise SystemExit(f"invalid {outcome}/{mode} observation")
                samples[outcome][mode].append(elapsed)
    summary = {outcome: {mode: statistics.median(times) for mode, times in modes.items()}
               for outcome, modes in samples.items()}
    receipt = {"samples": samples, "median_seconds": summary, "iterations": args.iterations,
               "scope": "paired complete process times for one checked archive fixture; not whole-learner or held-out performance"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))


if __name__ == "__main__":
    main()
