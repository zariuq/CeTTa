#!/usr/bin/env python3
"""Matched common paths and separately measured parser-artifact phases."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import statistics
import subprocess
import time

from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed, output_values

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-cetta", type=Path, required=True)
    parser.add_argument("--candidate-cetta", type=Path, required=True)
    parser.add_argument("--native-benchmark", type=Path, required=True)
    parser.add_argument("--baseline", default="44f35567")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=7)
    args = parser.parse_args()
    if args.samples < 3:
        parser.error("at least three alternating samples are required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binaries = {"base": args.base_cetta.resolve(), "candidate": args.candidate_cetta.resolve()}
    native = args.native_benchmark.resolve()
    baseline_tree = subprocess.check_output(
        ["git", "rev-parse", args.baseline + "^{tree}"], cwd=ROOT, text=True).strip()
    libraries = {}
    baseline_root = output / "baseline-runtime"
    baseline_root.mkdir(exist_ok=True)
    baseline_binary = baseline_root / "cetta"
    if not baseline_binary.exists():
        os.link(binaries["base"], baseline_binary)
    if digest(baseline_binary) != digest(binaries["base"]):
        raise RuntimeError("the preserved baseline binary differs; use a new output directory")
    binaries["base"] = baseline_binary
    for relative in ("lib/langdef.metta", "lib/petta/langdef.metta", "lib/json.metta",
                     "lib/petta/json.metta", "lib/prime/json.metta"):
        saved = baseline_root / relative
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(subprocess.check_output(
            ["git", "show", args.baseline + ":" + relative], cwd=ROOT))
    for language in ("he", "petta", "prime"):
        relative = "lib/petta/langdef.metta" if language == "petta" else "lib/langdef.metta"
        libraries[language] = {"base": baseline_root / relative, "candidate": ROOT / relative}
    authority = ROOT / "langdef/bnf/plain_bnf_source_v1.metta"
    profile = ROOT / "langdef/bnf/plain_bnf_parser_profile_v1.metta"
    load = form("langdef:load-authored-parser", text(str(authority)), text(str(profile)))
    sample = text('<S> ::= "a"\n')
    json_sample = text('{"id":42,"method":"tools/call","params":{"name":"shell"}}')
    json_roundtrip = form("==", form("json:stringify", form("json:parse", json_sample)), json_sample)
    rows = []
    for language in ("he", "petta", "prime"):
        for phase in ("startup", "json-roundtrip-100", "construct-5", "receipt-parse-100"):
            sources = {}
            for label in binaries:
                imported = "!(import! &self langdef)\n"
                if phase == "startup":
                    source = "!(+ 1 2)\n"
                elif phase == "json-roundtrip-100":
                    source = "!(import! &self json)\n" + ("!" + json_roundtrip + "\n") * 100
                elif phase == "construct-5":
                    source = imported + ("!" + lets([("$parser", load)],
                        form("langdef:close-authored-parser", "$parser")) + "\n") * 5
                else:
                    results = [(f"$value-{i}", form("langdef:parse-authored-text", "$parser", sample))
                               for i in range(100)]
                    body = "(function (return (ParserBenchmarkValuesV1 " + " ".join(name for name, _ in results) + ")))"
                    source = imported + "!" + lets([("$parser", load)] + results +
                        [("$close", form("langdef:close-authored-parser", "$parser"))], body) + "\n"
                path = output / f"{language}-{phase}-{label}.metta"
                path.write_text(source)
                sources[label] = path
            samples = {label: [] for label in binaries}
            for repeat in range(args.samples):
                order = list(binaries) if repeat % 2 == 0 else list(reversed(binaries))
                for label in order:
                    command = [str(binaries[label]), "--lang", language]
                    if language != "prime":
                        command += ["--profile", "extended"]
                    command.append(str(sources[label]))
                    before = resource.getrusage(resource.RUSAGE_CHILDREN)
                    started = time.perf_counter()
                    result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                            text=True, timeout=180)
                    wall = time.perf_counter() - started
                    after = resource.getrusage(resource.RUSAGE_CHILDREN)
                    if result.returncode:
                        raise AssertionError((language, phase, label, result.returncode, result.stderr))
                    values = output_values(result.stdout)
                    if phase == "startup":
                        valid = values == [3]
                    elif phase == "receipt-parse-100":
                        valid = len(values) == 1 and headed(values[0], "ParserBenchmarkValuesV1", 100) and \
                            all(headed(value, "LangDef:AuthoredParseAccepted", 4) for value in values[0][1:])
                    else:
                        expected = 100 if phase == "json-roundtrip-100" else 5
                        expected_lines = ["true"] * (expected + 1) if language == "petta" else \
                            ["[()]"] + ["[True]"] * expected
                        valid = result.stdout.splitlines() == expected_lines
                    if not valid:
                        raise AssertionError((language, phase, label, result.stdout[:1000]))
                    samples[label].append({"wall_s": wall, "cpu_s":
                        after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime})
            medians = {label: {clock: statistics.median(row[clock] for row in observations)
                               for clock in ("wall_s", "cpu_s")}
                       for label, observations in samples.items()}
            row = {"language": language, "phase": phase, "samples": samples, "medians": medians,
                   "wall_ratio": medians["candidate"]["wall_s"] / medians["base"]["wall_s"],
                   "cpu_ratio": medians["candidate"]["cpu_s"] / medians["base"]["cpu_s"]}
            rows.append(row)
            print(json.dumps({key: row[key] for key in ("language", "phase", "medians", "wall_ratio", "cpu_ratio")}), flush=True)
    phases = []
    for _ in range(args.samples):
        result = subprocess.run([str(native), str(output / "native-parser.tpp")], cwd=ROOT,
                                text=True, capture_output=True, check=True, timeout=180)
        phases.append(json.loads(result.stdout))
    receipt = {"format": "ParserArtifactPhaseBenchmarkV1", "baseline": args.baseline,
        "baseline_tree": baseline_tree, "samples": args.samples, "common_rows": rows,
        "native_phase_samples": phases,
        "native_phase_median_ns": {key: statistics.median(row[key] for row in phases) for key in phases[0]},
        "sha256": {str(path): digest(path) for path in list(binaries.values()) + [native, authority, profile] +
            [path for pair in libraries.values() for path in pair.values()]},
        "scope": "Common-path timings include process startup, language evaluation and observation; native phases exclude those. Checked restoration includes decoding plus its explicitly requested audit. No universal speed or no-loss claim."}
    (output / "summary.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
