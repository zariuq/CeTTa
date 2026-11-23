#!/usr/bin/env python3
"""Measure compiled-definition survival under unrelated edits in HE and PeTTa.

Three measured workloads across scaling N in both dialects:
  1. memo_add:   interleaving add-atom of new function definitions on an
                 unrelated head `memo` with calls to a compiled recursive
                 function `work`.
  2. data_write: interleaving add-atom (and remove-atom) of data atoms `(log ...)`
                 with calls to `work`.
  3. importer_add: importing `work` from a module and adding unrelated function
                 definitions `(= (local-memo $i) $i)` in the importer while
                 calling `(mod-work ...)`.

Reports wall time (ms), ms/step, and growth ratio (largest N / smallest N per
step) plus exact answer checks. Kept out of test-all (timing check).
"""

import argparse
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def make_memo_add_files(n: int) -> dict:
    # Single-clause non-overlapping recursive function + helper so both HE and
    # PeTTa get an exact deterministic result on every step while interleaving
    # unrelated function definitions `(= (memo $i) $i)`.
    code = (
        "(= (step $k $acc) (if (<= $k 0) $acc (step (- $k 1) (+ $acc $k))))\n"
        "(= (work $n) (+ (step $n 0) (step (- $n 1) 0)))\n"
        "(= (run-memo $i $n $acc)\n"
        "   (if (>= $i $n) $acc\n"
        "       (let $_ (add-atom &self (= (memo $i) $i))\n"
        "         (let $v (work 20)\n"
        "           (run-memo (+ $i 1) $n (+ $acc $v))))))\n"
        f"!(run-memo 0 {n} 0)\n"
    )
    return {"main.metta": code}


def make_data_write_files(n: int) -> dict:
    code = (
        "(= (step $k $acc) (if (<= $k 0) $acc (step (- $k 1) (+ $acc $k))))\n"
        "(= (work $n) (+ (step $n 0) (step (- $n 1) 0)))\n"
        "(= (run-data $i $n $acc)\n"
        "   (if (>= $i $n) $acc\n"
        "       (let $_ (add-atom &self (log-entry $i val))\n"
        "         (let $v (work 20)\n"
        "           (run-data (+ $i 1) $n (+ $acc $v))))))\n"
        f"!(run-data 0 {n} 0)\n"
    )
    return {"main.metta": code}


def make_importer_add_files(n: int) -> dict:
    mod = (
        "(= (mod-step $k $acc) (if (<= $k 0) $acc (mod-step (- $k 1) (+ $acc $k))))\n"
        "(= (mod-work $n) (+ (mod-step $n 0) (mod-step (- $n 1) 0)))\n"
    )
    main = (
        "!(import! &self calc_mod)\n"
        "(= (run-imp $i $n $acc)\n"
        "   (if (>= $i $n) $acc\n"
        "       (let $_ (add-atom &self (= (local-memo $i) $i))\n"
        "         (let $v (mod-work 20)\n"
        "           (run-imp (+ $i 1) $n (+ $acc $v))))))\n"
        f"!(run-imp 0 {n} 0)\n"
    )
    return {"calc_mod.metta": mod, "main.metta": main}


WORKLOADS = [
    ("memo_add", make_memo_add_files, [100, 200, 400, 800]),
    ("data_write", make_data_write_files, [100, 200, 400, 800]),
    ("importer_add", make_importer_add_files, [100, 200, 400, 800]),
]


def expected_answer(n: int) -> int:
    # step(20, 0) = 210, step(19, 0) = 190, work(20) = 400
    return 400 * n


def run_once(cetta: Path, case_dir: Path, flags: list, expected_val: int, is_he: bool) -> float:
    t0 = time.perf_counter()
    proc = subprocess.run(
        [str(cetta), *flags, str(case_dir / "main.metta")],
        cwd=case_dir,
        capture_output=True,
        text=True,
        timeout=60,
    )
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    if proc.returncode != 0:
        raise RuntimeError(f"cetta failed (rc={proc.returncode}): {proc.stderr}")
    lines = [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()]
    want = f"[{expected_val}]" if is_he else str(expected_val)
    if not lines or lines[-1] != want:
        raise RuntimeError(f"wrong answer: want {want!r}, got {lines[-1:]!r}")
    return elapsed_ms


def measure(cetta: Path, work_dir: Path, repeats: int, dialects: list):
    results = []
    for dialect_name, flags, is_he in dialects:
        for wl_name, builder, ns in WORKLOADS:
            for n in ns:
                case_dir = Path(tempfile.mkdtemp(dir=work_dir))
                try:
                    for rel, content in builder(n).items():
                        (case_dir / rel).write_text(content, encoding="utf-8")
                    # Warmup + measured runs
                    run_once(cetta, case_dir, flags, expected_answer(n), is_he)
                    samples = [
                        run_once(cetta, case_dir, flags, expected_answer(n), is_he)
                        for _ in range(repeats)
                    ]
                    med_ms = statistics.median(samples)
                    us_per_step = (med_ms * 1000.0) / n
                    results.append((dialect_name, wl_name, n, med_ms, us_per_step))
                finally:
                    shutil.rmtree(case_dir, ignore_errors=True)
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cetta", type=Path)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()

    cetta = args.cetta.resolve()
    dialects = [
        ("he", ["--lang", "he"], True),
        ("petta", ["--lang", "petta"], False),
    ]
    work = Path(tempfile.mkdtemp(prefix="cetta-compiled-survival-"))
    try:
        entries = measure(cetta, work, args.repeats, dialects)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print(f"{'dialect':<8} {'workload':<14} {'N':>6} {'median_ms':>10} {'us/step':>10}")
    print("-" * 52)
    for d, w, n, ms, us in entries:
        print(f"{d:<8} {w:<14} {n:>6} {ms:>10.2f} {us:>10.2f}")


if __name__ == "__main__":
    main()
