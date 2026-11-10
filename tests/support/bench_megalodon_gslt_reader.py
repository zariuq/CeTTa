#!/usr/bin/env python3
"""Scaling of CeTTa running the Megalodon syntax presentations as PeTTa.

Generates Megalodon documents of N items (definitions and admitted
theorems over a small notation, which Megalodon checks) and
times CeTTa reading each with the presentations langdef-compile
petta-direct makes of langdef/megalodon/ (MgReadItemV1, one item per step),
the way tests/megalodon/compare_gslt_reader.py drives them.  Read linearly,
doubling N doubles the time.

usage: bench_megalodon_gslt_reader.py CETTA [--program FILE] [--sizes 25,50,100,200] [--work DIR]
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "megalodon"))
import compare_gslt_reader as reader  # noqa: E402

PRELUDE = """Definition Plus : set -> set -> set := fun x y : set => x.
Infix + 360 right := Plus.
"""


def document(n):
    out = [PRELUDE]
    for i in range(n):
        out.append("Definition d%d : set -> set := fun x : set => x + x.\n" % i)
        out.append("Theorem t%d : forall x y : set, forall P : set -> prop, P (d%d x + y) -> P (d%d x + y).\n"
                   "Admitted.\n" % (i, i, i))
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cetta")
    ap.add_argument("--program", default="runtime/megalodon/megalodon_reader_v1.petta.metta")
    ap.add_argument("--sizes", default="25,50,100,200")
    ap.add_argument("--work", default="runtime/bench-megalodon-gslt-reader")
    ap.add_argument("--timeout", type=float, default=1800.0)
    args = ap.parse_args()
    work = pathlib.Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    program = pathlib.Path(args.program).read_text()
    print("%8s %8s %10s %8s %s" % ("items", "bytes", "seconds", "ratio", "outcome"))
    previous = None
    for n in [int(s) for s in args.sizes.split(",")]:
        doc = work / ("doc_%d.mg" % n)
        doc.write_text(document(n))
        (status, payload), elapsed = reader.run_cetta(os.path.abspath(args.cetta), program, str(doc),
                                                      "items", work, args.timeout, [])
        ratio = "" if previous is None else "%.1fx" % (elapsed / previous)
        outcome = "%s, %d items read" % (status, len(payload)) if status == reader.OK else status
        print("%8d %8d %10.2f %8s %s" % (n, doc.stat().st_size, elapsed, ratio, outcome), flush=True)
        previous = elapsed
    return 0


if __name__ == "__main__":
    sys.exit(main())
