#!/usr/bin/env python3
"""The native kernel's verdicts over the Megalodon example corpus.

Runs mg-check on every document with the options
tests/megalodon/corpus_checks.tsv gives it, and totals what the reports
say: theorems verified, checked under document axioms, admitted, left to
automation (aby), dependent, unfinished and out of budget.  A document
Megalodon accepts must not fail here (exit 2), and one it rejects must;
the reference statuses come from the cache compare_elab_rows.py fills.  A
report that is not a complete verdict is an error.

With --work, mg-check runs verbose and the largest work any theorem's
checking took is reported, per document and overall: the measure the
default --theorem-work budget is set from.

usage: corpus_verdicts.py --tool MG_CHECK --root EXAMPLES --reference DIR [--jobs N] [--work]
                          [--theorem-work UNITS] [FILE...]
"""
import argparse
import collections
import concurrent.futures
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from compare_elab_rows import documents, key  # noqa: E402

SUMMARY = re.compile(
    r"summary: (\d+) theorems: (\d+) checked \((\d+) verified\), (\d+) admitted, (\d+) aby, "
    r"(\d+) dependent, (\d+) unfinished, (\d+) out of budget; "
    r"(\d+) document axioms \((\d+) approved\)")
FIELDS = ("theorems", "checked", "verified", "admitted", "aby", "dependent",
          "unfinished", "exhausted", "document_axioms", "approved")
WORK = re.compile(r"work: the costliest theorem took (\d+) units")


def run_one(tool, rel, dargs, reference, timeout, extra):
    try:
        p = subprocess.run([tool] + extra + dargs, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return rel, "timeout", None, None, None
    out = p.stdout.decode("utf-8", "replace")
    m = SUMMARY.search(out)
    w = WORK.search(out)
    verdict = out.strip().splitlines()[-1] if out.strip() else ""
    want = (reference / (key(rel) + ".status")).read_text().strip()
    return (rel, p.returncode, (dict(zip(FIELDS, map(int, m.groups()))) if m else None), (verdict, want),
            int(w.group(1)) if w else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--reference", required=True)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--work", action="store_true")
    ap.add_argument("--theorem-work")
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    extra = (["--verbose"] if args.work else []) + (
        ["--theorem-work", args.theorem_work] if args.theorem_work is not None else [])
    work = []
    reference = pathlib.Path(args.reference)
    docs = documents(args.root, args.files)
    totals = collections.Counter()
    outcomes = collections.Counter()
    problems = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(run_one, args.tool, rel, dargs, reference, args.timeout, extra)
                   for rel, dargs in docs]
        for f in concurrent.futures.as_completed(futures):
            rel, status, counts, verdicts, most = f.result()
            if most is not None:
                work.append((most, rel))
            if status == "timeout" or counts is None:
                problems.append((rel, "no report (%s)" % status))
                continue
            verdict, want = verdicts
            outcomes[status] += 1
            totals.update(counts)
            if "ledger is incomplete" in verdict:
                problems.append((rel, verdict))
            elif want == "0" and status == 2:
                problems.append((rel, "Megalodon accepts it, mg-check: " + verdict))
            elif want != "0" and status != 2:
                problems.append((rel, "Megalodon rejects it (%s), mg-check: %s" % (want, verdict)))
    for rel, why in sorted(problems):
        print("PROBLEM %s: %s" % (rel, why))
    for most, rel in sorted(work, reverse=True)[:10] if args.work else []:
        print("WORK %s: the costliest theorem took %d units" % (rel, most))
    print("(MegalodonCorpusVerdicts docs=%d verified=%d not_verified=%d failed=%d %s problems=%d)" % (
        len(docs), outcomes[0], outcomes[1], outcomes[2],
        " ".join("%s=%d" % (k, totals[k]) for k in FIELDS), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
