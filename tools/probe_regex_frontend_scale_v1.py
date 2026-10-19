#!/usr/bin/env python3
"""Measure one prepared regex parser on size-matched source families."""

import argparse
import json
from pathlib import Path
import resource
import subprocess
import time

from gslt2parse_schema_v1 import Symbol
from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed, output_values

ROOT = Path(__file__).resolve().parents[1]
SELECTION = ('(ParserSelectionV1 (LCons (ParserRejectFollowingClassV1 '
             '"re-src:backref" "RxDigitClass") LNil))')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sizes", type=int, nargs="+", default=[1000, 2000, 4000, 8000, 16000])
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--native-probe", type=Path,
                        help="measure native recognition/replay separately instead of the public wrapper")
    parser.add_argument("--reference-probe", type=Path,
                        help="compare exact general forests with a baseline native probe")
    parser.add_argument("--families", nargs="+", choices=["keywords", "literal", "nesting"],
                        default=["keywords", "literal", "nesting"])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for family in args.families:
        for size in args.sizes:
            pattern = ("|".join(f"keyword{i:05d}" for i in range(size)) if family == "keywords"
                       else "a" * size if family == "literal" else "(?:" * size + "a" + ")" * size)
            (args.output / f"{family}-{size}.pattern").write_text(pattern, encoding="utf-8")
            for repeat in range(args.repeats):
                name = f"{family}-{size}-{repeat}"
                if args.native_probe:
                    raw = args.output / f"{family}-{size}.pattern"
                    records = {}
                    for engine, binary, suffix in (
                        ("reference", args.reference_probe, []),
                        ("general", args.native_probe, []),
                        ("corridor", args.native_probe, ["lr"]),
                    ):
                        if not binary:
                            continue
                        result = subprocess.run([str(binary.resolve()), str(raw), *suffix], cwd=ROOT,
                                                text=True, capture_output=True, timeout=300)
                        if result.returncode:
                            raise AssertionError((name, engine, result.returncode, result.stderr))
                        row = json.loads(result.stdout)
                        records[engine] = row
                        (args.output / f"{name}-{engine}.json").write_text(result.stdout)
                    if "reference" in records:
                        for key in ("accepted", "values", "descriptors", "gss", "nodes", "choices", "forest_sha256", "value_sha256"):
                            assert records["reference"][key] == records["general"][key], (name, key, records)
                    if records["corridor"].get("corridor") != "needs-general":
                        for key in ("accepted", "values", "value_sha256"):
                            assert records["corridor"][key] == records["general"][key], (name, key, records)
                    print(json.dumps(dict(family=family, size=size, repeat=repeat, source_bytes=len(pattern.encode()),
                                          observations=records)), flush=True)
                    continue
                source = args.output / (name + ".metta")
                source.write_text('!(import! &self langdef)\n!(import! &self system)\n!' + lets([
                    ("$reader", '(langdef:load-baked-parser "regex-pattern-reader")'),
                    ("$start", '(system:monotonic-ns)'),
                    ("$answer", form("langdef:parser-parse-text-selected", "$reader", text(pattern), SELECTION)),
                    ("$finish", '(system:monotonic-ns)'),
                    ("$elapsed", '(- $finish $start)'),
                    ("$summary", '(case $answer (((ParserAcceptedV1 $values $span) '
                         '(RegexFrontendScaleV1 accepted $span $elapsed)) '
                         '($other (RegexFrontendScaleV1 refused $other $elapsed))))'),
                    ("$closed", '(langdef:close-parser $reader)'),
                ], '(function (return $summary))') + '\n', encoding="utf-8")
                before = resource.getrusage(resource.RUSAGE_CHILDREN)
                started = time.monotonic_ns()
                result = subprocess.run([str(args.cetta.resolve()), "--lang", "he", "--profile", "extended", str(source)],
                                        cwd=ROOT, text=True, capture_output=True, timeout=300)
                elapsed = time.monotonic_ns() - started
                after = resource.getrusage(resource.RUSAGE_CHILDREN)
                (args.output / (name + ".stdout")).write_text(result.stdout)
                (args.output / (name + ".stderr")).write_text(result.stderr)
                rows = [row for row in output_values(result.stdout) if headed(row, "RegexFrontendScaleV1", 3)]
                if result.returncode or len(rows) != 1 or rows[0][1] != Symbol("accepted"):
                    raise AssertionError((name, result.returncode, rows, result.stderr))
                print(json.dumps(dict(family=family, size=size, repeat=repeat, source_bytes=len(pattern.encode()),
                    parse_ns=rows[0][3], process_ns=elapsed,
                    process_cpu_ns=round((after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime) * 1e9))), flush=True)


if __name__ == "__main__":
    main()
