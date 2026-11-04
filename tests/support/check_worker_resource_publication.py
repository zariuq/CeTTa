#!/usr/bin/env python3
"""Published worker resources preserve identity, later writes and retained reads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from compare_answers import collapse_bag

ROOT = Path(__file__).resolve().parents[2]

CASES = [
    (
        'space',
        """!(pragma! num-threads THREADS)
!(bind! &out (new-state (new-space)))
!(let $o (get-state &out)
  (collapse (hyperpose
    ((let $s (new-space)
       (let $_ (add-atom $s (value 11)) (add-atom $o (kept 1 $s))))
     (let $s (new-space)
       (let $_ (add-atom $s (value 12)) (add-atom $o (kept 2 $s))))))))
!(collapse (match (get-state &out) (kept $tag $s)
  ($tag (collapse (match $s (value $x) $x)))))
!(+ 20 22)
""",
        ['()', 'true', '(true true)', '((1 (11)) (2 (12)))', '42'],
        [2, 3],
    ),
    (
        'alias',
        """!(pragma! num-threads THREADS)
!(bind! &out (new-state (new-space)))
!(let $o (get-state &out)
 (collapse
  (let $r (hyperpose
   ((let $s (new-space)
     (let $_ (add-atom $s (key 11))
      (let $_ (add-atom $o (kept 1 $s $s))
       (let $_ (add-atom $s (key 13)) $s))))
    (let $s (new-space)
     (let $_ (add-atom $s (key 12))
      (let $_ (add-atom $o (kept 2 $s $s))
       (let $_ (add-atom $s (key 14)) $s))))))
   (match $o (kept $tag $s $alias)
    (if (== $r $s)
     (if (== $s $alias) (match $s (key $x) ($tag $x)) alias_mismatch)
     (empty))))))
!(+ 20 22)
""",
        ['()', 'true', '((1 11) (1 13) (2 12) (2 14))', '42'],
        [2],
    ),
    (
        'retained-after-delete',
        """!(pragma! num-threads THREADS)
!(bind! &out (new-state (new-space)))
(= (churn $n) (if (== $n 0) done (let $d (new-space) (churn (- $n 1)))))
!(bind! &saved (new-state
 (let $o (get-state &out)
  (collapse (hyperpose
   ((let $s (new-space)
     (let $_ (add-atom $s (key 11))
      (let $_ (add-atom $o (kept 1 $s $s))
       (let $_ (add-atom $s (key 13)) $s))))
    (let $s (new-space)
     (let $_ (add-atom $s (key 12))
      (let $_ (add-atom $o (kept 2 $s $s))
       (let $_ (add-atom $s (key 14)) $s))))))))))
!(let ($a $b) (get-state &saved)
 (let $_ (remove-atom (get-state &out) (kept 1 $a $a))
  (let $_ (remove-atom (get-state &out) (kept 2 $b $b)) true)))
!(size-atom (collapse (match (get-state &out) (kept $tag $s $alias) t)))
!(churn 8000)
!(let ($a $b) (get-state &saved) (collapse (match $a (key $x) $x)))
!(let ($a $b) (get-state &saved) (collapse (match $b (key $x) $x)))
!(+ 20 22)
""",
        ['()', 'true', 'true', 'true', '0', 'done', '(11 13)', '(12 14)', '42'],
        [6, 7],
    ),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    payload_binary = Path(os.environ.get("CETTA_WRAPPED_BIN", binary)).resolve(strict=True)
    digest = hashlib.sha256(payload_binary.read_bytes()).hexdigest()
    receipt = {"binary_sha256": digest,
        "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "runs": [], "pass": False}
    with tempfile.TemporaryDirectory(prefix="cetta-worker-publication-") as directory:
        for name, source, expected, bag_lines in CASES:
            for threads in (1, 2):
                text = source.replace("THREADS", str(threads))
                path = Path(directory) / f"{name}-{threads}.metta"
                path.write_text(text)
                for reference in ("0", "1"):
                    for stress in ("0", "1"):
                        env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1",
                            CETTA_PETTA_MACHINE_STATS="0",
                            CETTA_OPEN_EQUATIONS_REFERENCE=reference,
                            CETTA_PREPARED_PURE_REFERENCE=reference,
                            CETTA_OEM_COLLECT_STRESS=stress)
                        command = [str(binary), "--lang", "petta", str(path)]
                        run = subprocess.run(command, cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=120)
                        actual = run.stdout.splitlines()
                        equal = len(actual) == len(expected) and all(
                            collapse_bag(a, petta=True) == collapse_bag(e, petta=True)
                            if index in bag_lines else a == e
                            for index, (a, e) in enumerate(zip(actual, expected)))
                        passed = run.returncode == 0 and equal and not run.stderr
                        cell = {"case": name, "threads": threads,
                            "reference": reference, "collection_stress": stress,
                            "source_sha256": hashlib.sha256(text.encode()).hexdigest(),
                            "rc": run.returncode, "actual": actual,
                            "expected": expected, "stderr": run.stderr,
                            "pass": passed}
                        receipt["runs"].append(cell)
                        if args.output:
                            args.output.write_text(json.dumps(receipt, indent=2) + "\n")
                        if not passed:
                            print(json.dumps(cell, indent=2))
                            return 1
    receipt["ending_binary_sha256"] = hashlib.sha256(payload_binary.read_bytes()).hexdigest()
    receipt["pass"] = receipt["ending_binary_sha256"] == digest
    if args.output:
        args.output.write_text(json.dumps(receipt, indent=2) + "\n")
    if not receipt["pass"]:
        raise AssertionError("publication executable changed during the gate")
    print(f"PASS: {len(receipt['runs'])} worker publication, alias and retained-reader controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
