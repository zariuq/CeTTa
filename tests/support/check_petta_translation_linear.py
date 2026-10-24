#!/usr/bin/env python3
"""Extending explicitly evaluated data revisits new structure, not its whole tail."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
WORK = "petta-translation-value-node"
REUSE = "petta-translation-value-reuse"
HOST = "open-equation-host"
HOST_EXPORT = "open-equation-host-export-bytes"
HOST_IMPORT = "open-equation-host-import-bytes"
TRANSPORT = "atom-transport-node"
RULES = """
(= (push $i $xs) (eval (node $i $xs)))
(= (touch $n $xs)
   (if (== $n 0) $xs
       (let $ignored (eval (probe 1)) (touch (- $n 1) $xs))))
(= (grow $i $n $xs)
   (if (== $i $n) $xs (grow (+ $i 1) $n INSERT)))
(= (length-of () $n) $n)
(= (length-of (node $h $t) $n) (length-of $t (+ $n 1)))
"""


def run(binary, directory, n, quadratic):
    insertion = "(push $i $xs)"
    env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1")
    if quadratic:
        insertion = "(touch $i (push $i $xs))"
        # The control's repeated evaluations must run where this counter
        # observes them.  The prepared machine compiles the touching loop,
        # its evaluation of a closed term included, and then repeats it
        # without translation work.
        env["CETTA_PREPARED_PURE_REFERENCE"] = "1"
    source = RULES.replace("INSERT", insertion)
    source += f"!(length-of (grow 0 {n} ()) 0)\n"
    path = Path(directory) / f"{'quadratic' if quadratic else 'linear'}-{n}.metta"
    path.write_text(source)
    proc = subprocess.run([binary, "--lang", "petta", "--emit-runtime-stats", str(path)],
                          cwd=ROOT, env=env,
                          capture_output=True, text=True, timeout=120)
    if proc.returncode or proc.stdout.strip() != str(n):
        raise AssertionError(f"Wrong answer at {n}: {proc.returncode}, {proc.stdout}, {proc.stderr}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    assert counters.get(WORK, 0) > 0, "Translation work counter missing or inactive"
    assert counters.get(REUSE, 0) > 0, "No surviving translation evidence was reused"
    return counters[WORK]


def run_deep_call(binary, directory, depth):
    term = "(+ 1 2)"
    for _ in range(depth):
        term = f"(opaque-box {term})"
    path = Path(directory) / f"deep-call-{depth}.metta"
    path.write_text(f"!(eval {term})\n")
    proc = subprocess.run(
        [binary, "--lang", "petta", "--emit-runtime-stats", str(path)],
        cwd=ROOT, capture_output=True, text=True, timeout=120)
    if proc.returncode:
        raise AssertionError(
            f"Deep call failed at {depth}: {proc.returncode}, {proc.stderr}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    return counters[WORK]


def run_host_queue(binary, directory, n):
    source = """
(= (enqueue $e (queue $in $out $size))
   (queue (node $e $in) $out (+ $size 1)))
(= (empty-queue) (queue () () 0))
(: push-both (-> Expression Expression Expression))
(= (push-both $items $queue) (eval (foldl enqueue $items $queue)))
(= (grow $queue $i $n)
   (if (== $i $n) $queue
       (grow (push-both ($i (+ $i 1)) $queue) (+ $i 1) $n)))
(= (qsize (queue $in $out $size)) $size)
""" + f"!(qsize (grow (empty-queue) 0 {n}))\n"
    path = Path(directory) / f"host-queue-{n}.metta"
    path.write_text(source)
    proc = subprocess.run(
        [binary, "--lang", "petta", "--emit-runtime-stats", str(path)],
        cwd=ROOT, capture_output=True, text=True, timeout=120)
    if proc.returncode or proc.stdout.strip() != str(2 * n):
        raise AssertionError(
            f"Wrong host queue answer at {n}: "
            f"{proc.returncode}, {proc.stdout}, {proc.stderr}")
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    return counters


def ratios(work):
    return [b / a for a, b in zip(work, work[1:])]


binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="translation-work-", dir=ROOT / "runtime") as directory:
    positive = [run(binary, directory, n, False) for n in (1000, 2000, 4000)]
    negative = [run(binary, directory, n, True) for n in (256, 512)]
    deep_call = [run_deep_call(binary, directory, n) for n in (256, 512)]
    host_queue = [run_host_queue(binary, directory, n)
                  for n in (512, 1024, 2048)]
assert max(ratios(positive)) < 2.8, f"Data-tail translation is not linear: {positive}"
assert min(ratios(negative)) > 3.2, f"Deliberate repeated evaluation escaped the control: {negative}"
assert max(ratios(deep_call)) < 2.8, \
    f"A call under inert constructors is not planned linearly: {deep_call}"
assert all(row[HOST] == n for row, n in zip(host_queue, (512, 1024, 2048)))
assert all(row[HOST_IMPORT] == 0 for row in host_queue), \
    f"Stable host values were copied back into the region: {host_queue}"
for counter in (HOST_EXPORT, TRANSPORT):
    work = [row[counter] for row in host_queue]
    assert max(ratios(work)) < 2.8, \
        f"Stable host transport is not linear ({counter}): {work}"
print(f"PASS: explicit data growth has linear translation work {positive}; "
      f"deep calls stay linear {deep_call}; stable host values return without "
      f"an import copy; quadratic control {negative} is detected")
