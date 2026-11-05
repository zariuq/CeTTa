#!/usr/bin/env python3
"""Compare the evaluator's reading of .mg files with Megalodon's own parser.

For every .mg and .mgs file under the given roots, the dump tool (the items
as the evaluator receives them) and the reference parse oracle (built from
Megalodon's lexer.mll and parser.ml) must print the same items and fail the
same way: same exit status and the same message, positions included.  The
tokens are the presentations' own reading, compared by
compare_gslt_reader.py.  Prints a summary and every disagreement.
"""

import argparse
import pathlib
import subprocess
import sys
import time


def run(command, timeout):
    started = time.perf_counter()
    try:
        done = subprocess.run(command, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, None, None, timeout
    return done.returncode, done.stdout, done.stderr, time.perf_counter() - started


def normalized_error(stderr):
    text = stderr.decode("utf-8", "replace").strip()
    return text.replace("oracle failure: ", "failure: ")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", required=True)
    parser.add_argument("--parse-oracle", required=True)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--includes",
                        help="TSV of FILE<TAB>SIGNATURE, relative to the root, "
                             "for files read after an -I signature")
    parser.add_argument("roots", nargs="+")
    args = parser.parse_args()

    files = sorted(
        path
        for root in args.roots
        for pattern in ("*.mg", "*.mgs")
        for path in pathlib.Path(root).rglob(pattern)
        if path.is_file()
    )
    includes = {}
    if args.includes:
        base = pathlib.Path(args.roots[0])
        for line in pathlib.Path(args.includes).read_text().splitlines():
            if not line or line.startswith("#"):
                continue
            file_name, signature = line.split("\t")
            includes[(base / file_name).resolve()] = [str(base / signature)]
    agree = disagree = timeouts = 0
    reference_time = native_time = 0.0
    accepted = 0
    for path in files:
        for mode, oracle in (("--items", args.parse_oracle),):
            extra = []
            if mode == "--items":
                for signature in includes.get(path.resolve(), []):
                    extra += ["-I", signature]
            ref = run([oracle, *extra, str(path)], args.timeout)
            got = run([args.dump, mode, *extra, str(path)], args.timeout)
            if ref[0] is None or got[0] is None:
                timeouts += 1
                print(f"TIMEOUT {mode} {path}")
                continue
            reference_time += ref[3]
            native_time += got[3]
            same = (ref[0] == got[0] and ref[1] == got[1] and
                    (ref[0] == 0 or normalized_error(ref[2]) == normalized_error(got[2])))
            if same:
                agree += 1
                if mode == "--items" and ref[0] == 0:
                    accepted += 1
            else:
                disagree += 1
                print(f"DIFFER {mode} {path}: exit {ref[0]} vs {got[0]}; "
                      f"stdout {len(ref[1])} vs {len(got[1])} bytes")
                if ref[0] != 0 or got[0] != 0:
                    print(f"  reference: {normalized_error(ref[2])[:300]}")
                    print(f"  native:    {normalized_error(got[2])[:300]}")
    print(f"files {len(files)}; comparisons agree {agree}, differ {disagree}, "
          f"timeouts {timeouts}; files parsed to the end {accepted}")
    print(f"time: reference {reference_time:.2f}s, native {native_time:.2f}s")
    return 0 if disagree == 0 and timeouts == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
