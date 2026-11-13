#!/usr/bin/env python3
"""Retired open rows must not promote their payloads into permanent storage."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BINARY = str(Path(sys.argv[1]).resolve())


def run(depth, reference=False):
    program = f"""
(= (mk $n $last) (if (== $n 0) $last (f $v (mk (- $n 1) $last))))
!(bind! &s (new-space))
!(let $k (mk {depth} 0) (add-atom &s (st 0 $k)))
(= (turn) (collapse (match &s (st $c $k)
  (let $d (+ $c 1) (let $next (mk {depth} $d)
    (let $_r (remove-atom &s (st $c $k)) (add-atom &s (st $d $next))))))))
(= (count $n) (if (== $n 0) (collapse (match &s (st $c $k) $c))
    (let $_ (turn) (count (- $n 1)))))
!(count 250)
!(count 250)
!(count 250)
!(count 250)
"""
    env = dict(os.environ)
    env.pop("CETTA_OEM_STORED_REFERENCE", None)
    if reference:
        env["CETTA_OEM_STORED_REFERENCE"] = "1"
    # Exercise collection during each run, rather than measuring process RSS.
    env.update(CETTA_TERM_UNIVERSE_COLLECTION_TRACE="1",
               CETTA_TERM_UNIVERSE_COLLECTION_FLOOR="1024",
               CETTA_TERM_UNIVERSE_COLLECTION_BYTE_FLOOR="32768")
    with tempfile.TemporaryDirectory(prefix="open-row-retention-", dir=ROOT / "runtime") as directory:
        source = Path(directory) / "control.metta"
        source.write_text(program)
        result = subprocess.run([BINARY, "--lang", "petta", str(source)],
                                cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=60)
    expected = ["true", "(250)", "(500)", "(750)", "(1000)"]
    assert result.returncode == 0 and result.stdout.splitlines() == expected, (
        depth, reference, result.returncode, result.stdout, result.stderr[-1000:])
    samples = [int(value) for value in re.findall(r"persistent (\d+) supports", result.stderr)]
    assert len(samples) >= 3, "the control did not exercise collection"
    return samples[-1] - samples[0]


def qualified(growth):
    # Counter and compiler caches may grow. Retired payload size must not
    # multiply that growth; the wider payload is four times as deep.
    return growth[1] <= 2 * growth[0] + 65536


normal = [run(depth) for depth in (24, 96)]
assert qualified(normal), f"retired row payloads retained: {normal}"
reference = [run(depth, True) for depth in (24, 96)]
assert not qualified(reference), f"decoded-snapshot control did not detect retention: {reference}"
print(f"PASS: scoped rows preserve answers without retaining retired payloads; growth {normal}; decoded-snapshot control {reference}")
