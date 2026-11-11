#!/usr/bin/env python3
"""Time the Megalodon binary and the native checker on the same documents.

Each example document is checked by both, one process at a time, with the
options tests/megalodon/corpus_checks.tsv gives it.  Two comparisons:

  --mode rows     megalodon -sexprinfo against mg-sexprinfo: both print the
                  same rows (the native run may add -kernel, which also
                  admits every declaration and proof into the native
                  kernel); a document counts when both exit alike.
  --mode verdict  megalodon checking a document against mg-check, whose
                  kernel also rechecks every proof and reports the verdict;
                  a document counts when Megalodon accepts it and mg-check
                  says verified or not verified (exit 0 or 1), or Megalodon
                  rejects it and mg-check fails (exit 2).
  --mode aby      mg-check --prove-aby alone (Megalodon admits aby steps):
                  its wall time, peak memory, and the aby steps it proved
                  by search, on each FILE (Megalodon's hammer example
                  without one).

Documents where the two did not do corresponding work are left out of the
totals and listed.  Prints per-directory and total wall time, the peak
resident memory of each, the speedup, and the SHA-256 of both binaries.

usage: bench_elab.py [--megalodon BIN] --tool TOOL --root EXAMPLES
                     [--mode rows|verdict|aby] [--kernel] [--slowest N] [FILE...]

--slowest N also lists the N documents the native checker takes longest on.
"""
import argparse
import collections
import hashlib
import os
import pathlib
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from compare_elab_rows import documents  # noqa: E402


def sha256_of(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def timed(cmd):
    """Wall and CPU seconds and peak resident kilobytes of one process."""
    t0 = time.perf_counter()
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _, status, ru = os.wait4(p.pid, 0)
    wall = time.perf_counter() - t0
    p.returncode = os.waitstatus_to_exitcode(status)
    return p.returncode, wall, ru.ru_utime + ru.ru_stime, ru.ru_maxrss


def aby(tool, root, files):
    """Native aby on each document: the steps proved by search, the time
    and the peak memory it took."""
    for rel in files or ["hammer/100thms_12_h.mg"]:
        t0 = time.perf_counter()
        p = subprocess.Popen([tool, "--prove-aby", str(pathlib.Path(root) / rel)],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        out = p.stdout.read().decode(errors="replace")
        _, status, ru = os.wait4(p.pid, 0)
        wall = time.perf_counter() - t0
        print("%s: %.1f s, %.0f MB peak, exit %d" % (rel, wall, ru.ru_maxrss / 1024.0,
                                                   os.waitstatus_to_exitcode(status)))
        for line in out.splitlines():
            if line.startswith(("summary:", "aby:", "verdict:")):
                print("  " + line)
    print("(MegalodonBench mode=aby native_sha256=%s)" % sha256_of(tool)[:16])
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--megalodon")
    ap.add_argument("--tool", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--mode", choices=["rows", "verdict", "aby"], default="rows")
    ap.add_argument("--kernel", action="store_true")
    ap.add_argument("--slowest", type=int, default=0)
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    if args.mode == "aby":
        return aby(args.tool, args.root, args.files)
    if not args.megalodon:
        ap.error("--megalodon is required for --mode rows and --mode verdict")
    docs = documents(args.root, args.files)
    rows = collections.defaultdict(lambda: [0, 0.0, 0.0, 0.0, 0.0])
    peak = {"megalodon": 0, "native": 0}
    each = []
    skipped = []
    for rel, dargs in docs:
        group = rel.split("/")[0]
        flags = ["-sexprinfo"] if args.mode == "rows" else []
        st1, w1, c1, rss1 = timed([args.megalodon] + flags + dargs)
        native = [args.tool, "-kernel"] if args.kernel and args.mode == "rows" else [args.tool]
        st2, w2, c2, rss2 = timed(native + dargs)
        same_work = st1 == st2 if args.mode == "rows" else (st2 in (0, 1)) == (st1 == 0)
        if not same_work:
            skipped.append((rel, st1, st2))
            continue
        r = rows[group]
        r[0] += 1
        r[1] += w1
        r[2] += w2
        r[3] += c1
        r[4] += c2
        peak["megalodon"] = max(peak["megalodon"], rss1)
        peak["native"] = max(peak["native"], rss2)
        each.append((w2, w1, rss2, rss1, rel))
    total = [0, 0.0, 0.0, 0.0, 0.0]
    print("%-24s %6s %12s %12s %8s" % ("corpus", "docs", "megalodon s", "native s", "speedup"))
    for group in sorted(rows):
        r = rows[group]
        for i in range(5):
            total[i] += r[i]
        print("%-24s %6d %12.2f %12.2f %7.1fx" % (group, r[0], r[1], r[2], r[1] / r[2] if r[2] else 0.0))
    print("%-24s %6d %12.2f %12.2f %7.1fx" % ("total", total[0], total[1], total[2],
                                             total[1] / total[2] if total[2] else 0.0))
    if args.slowest:
        print("%-56s %10s %10s %10s %10s" % ("slowest", "megalodon s", "native s", "mg MB", "native MB"))
        for w2, w1, rss2, rss1, rel in sorted(each, reverse=True)[:args.slowest]:
            print("%-56s %10.3f %10.3f %10.1f %10.1f" % (rel[:56], w1, w2, rss1 / 1024.0, rss2 / 1024.0))
    for rel, st1, st2 in skipped:
        print("left out %s: megalodon exit %s, native exit %s" % (rel, st1, st2))
    print("(MegalodonBench mode=%s docs=%d left_out=%d megalodon_wall=%.2f native_wall=%.2f "
          "megalodon_cpu=%.2f native_cpu=%.2f megalodon_peak_rss_kb=%d native_peak_rss_kb=%d "
          "kernel=%s megalodon_sha256=%s native_sha256=%s)"
          % (args.mode, total[0], len(skipped), total[1], total[2], total[3], total[4],
             peak["megalodon"], peak["native"],
             "on" if args.kernel or args.mode == "verdict" else "off",
             sha256_of(args.megalodon)[:16], sha256_of(args.tool)[:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
