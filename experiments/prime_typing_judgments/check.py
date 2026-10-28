#!/usr/bin/env python3
"""Exercise the experimental judgment interface through a selected Prime binary."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
EXPECTED = {
    "he-rule-programs-generated.metta": ["[()]"] * 167,
    "hol-programs-generated.metta": ["[()]"] * 61,
    "named-readback-generated.metta": ["[()]"] * 32,
    "generated-family-packages.metta": ["[()]"] * 16,
    "judgment-transforms.metta": ["[()]"] * 15 + ["[1]"] + ["[()]"] * 2 + ["[1]"] + ["[()]"] * 3,
    "interactive-judgments.metta": ["[()]"] * 8 + ["[1]"] + ["[()]"] * 2 + ["[1]"] + ["[()]"] * 2 + ["[1]"] + ["[()]"] * 2,
    "query-judgments.metta": ["[()]"] * 22,
    "source-rule-programs.metta": ["[()]"] * 7,
    "open-rule-programs.metta": ["[()]"] * 24,
    "hol-rule-generated.metta": ["[()]"] * 14,
    "he-reference-generated.metta": ["[()]"] * 11,
    "declaration-packages.metta": ["[()]"] * 15,
    "conversion-programs.metta": ["[()]", "[()]", "[nat]", "[add]"]
        + ["[()]"] * 4 + ["[nat]"] + ["[()]"] * 5,
    "judgment-programs.metta": ["[()]", "[Nat]", "[add]", "[At]", "[pack]", "[use]"]
        + ["[()]"] * 23 + ["[Bit]"] + ["[()]"] * 13,
    "rule-generators.metta": ["[()]", "[Nat]"]
        + ["[()]"] * 9 + ["[Bit]"] + ["[()]"] * 7,
    "first-class-rules.metta": ["[()]", "[Nat]", "[inc]"] + ["[()]"] * 5,
    "syntax-effects.metta": ["[()]", "[Nat]"] + ["[()]"] * 8,
    "same-source-admission.metta": ["[Nat]"] + ["[()]"] * 3,
    "conflicting-conversion.metta": ["[Nat]", "[inc]"] + ["[()]"] * 6,
    "declaration-boundary.metta": [
        "[Nat]", "[Z]",
        "[(Undetermined (expected-formed-type))]",
        "[(Undetermined (expected-type-undetermined (undeclared-type-form Z)))]",
        "[(PrimeKernelQueryV1 (Judgment (type:check ill Z)) (Route outside))]",
        "[(Undetermined (expected-formed-type))]",
        "[(Undetermined (expected-type-undetermined (ill-formed-telescope-component 0 (undeclared-type-form (x : Mystery)))))]",
        "[(Undetermined (expected-formed-type))]",
    ],
}
OPEN_BOUNDARIES = {"same-source-admission.metta", "conflicting-conversion.metta"}
PROBE_EXPECTED = {
    "direct-producer.metta": ["[()]", "[Nat]", "[inc]", "[True]", "[()]", "[()]", "[True]"],
    "nested-producer.metta": ["[()]", "[Nat]", "[inc]"] + ["[()]"] * 4,
}

def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--working-dir", type=Path, default=HERE,
                        help="Working directory supplying runtime resources (default: this experiment)")
    parser.add_argument("--hol-host", action="store_true",
                        help="Also run the maintained native HOL bridge qualification")
    parser.add_argument("--probe-gaps", action="store_true",
                        help="Record additional nested evaluation paths separately")
    args = parser.parse_args()
    binary = args.cetta.resolve()
    working_dir = args.working_dir.resolve()
    if not working_dir.is_dir():
        parser.error("--working-dir must name an existing directory")
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
    owned = sorted(path for path in HERE.iterdir()
                   if path.suffix in {".metta", ".lean", ".txt", ".py"})
    owned += sorted((ROOT / "lib").rglob("*.metta"))
    if args.hol_host:
        owned.append(ROOT / "tests" / "hol_light" / "test_host.py")
    pins = {str(p.relative_to(ROOT)): digest(p) for p in owned}
    kernel = ROOT.parents[1] / "Mettapedia" / "MettaKernel" / "kernel"
    kernel_names = ("generic_open_rule_programs.metta", "generic_inference_checker_v0.metta",
                    "kernel_binding_decl_v1.metta", "kernel_binding_waist_v1.metta",
                    "gslt_checked_source_v1.metta")
    kernel_pins = {name: digest(kernel / name) for name in kernel_names}
    binary_before = digest(binary)
    outcomes = []
    for name, expected in EXPECTED.items():
        dialect = "he" if name == "he-reference-generated.metta" else "prime"
        try:
            completed = subprocess.run([str(binary), "--lang", dialect, str(HERE / name)],
                cwd=working_dir, text=True, capture_output=True, timeout=30)
            stdout, stderr, code = completed.stdout, completed.stderr, completed.returncode
        except subprocess.TimeoutExpired as error:
            stdout = (error.stdout or b"").decode() if isinstance(error.stdout, bytes) else (error.stdout or "")
            stderr = "Program exceeded the qualification deadline"
            code = None
        actual = stdout.splitlines()
        passed = code == 0 and actual == expected and not stderr
        assertions = len(re.findall(r"\(assertEqual(?=\s|\))", (HERE / name).read_text()))
        entry = {"program": name, "dialect": dialect, "passed": passed, "exit_code": code,
                 "expected_outputs": len(expected), "actual_outputs": len(actual),
                 "assertions": assertions,
                 "scope": "open-boundary reproduction" if name in OPEN_BOUNDARIES else "supported programming"}
        outcomes.append(entry)
        print(f"{name}: {'PASS' if passed else 'FAIL'} ({len(actual)}/{len(expected)} outputs)")
        if not passed:
            print(stdout)
            print(stderr)
            for index, (left, right) in enumerate(zip(actual, expected), 1):
                if left != right:
                    print(f"first differing output {index}: expected {right!r}, got {left!r}")
                    break
        if args.output_dir:
            (args.output_dir / (name + ".out")).write_text(stdout)
            (args.output_dir / (name + ".err")).write_text(stderr)
    gap_probes = []
    if args.probe_gaps:
        for name, expected in PROBE_EXPECTED.items():
            try:
                completed = subprocess.run([str(binary), "--lang", "prime", str(HERE / name)],
                    cwd=working_dir, text=True, capture_output=True, timeout=10)
                gap_probes.append({"program": name, "exit_code": completed.returncode,
                                   "stdout": completed.stdout, "stderr": completed.stderr,
                                   "meets_expected_results": completed.returncode == 0
                                       and completed.stdout.splitlines() == expected
                                       and not completed.stderr,
                                   "scope": "Separate compatibility probe; reproducing a failure closes no gate"})
            except subprocess.TimeoutExpired:
                gap_probes.append({"program": name, "exit_code": None, "status": "deadline-exceeded",
                                   "scope": "Observation only; the supported staged route is checked separately"})
            print(f"{name}: RECORDED PROBE (excluded from passing controls)")
    hol_host = None
    if args.hol_host:
        try:
            host = subprocess.run(["python3", str(ROOT / "tests/hol_light/test_host.py"), str(binary)],
                                  cwd=ROOT, text=True, capture_output=True, timeout=120)
            summary = re.fullmatch(r"HolHostSummary ([1-9][0-9]*) 0\n", host.stdout)
            hol_host = {"passed": host.returncode == 0 and summary is not None and not host.stderr,
                        "checks": int(summary[1]) if summary else None, "exit_code": host.returncode}
            if args.output_dir:
                (args.output_dir / "hol-host.out").write_text(host.stdout)
                (args.output_dir / "hol-host.err").write_text(host.stderr)
        except subprocess.TimeoutExpired:
            hol_host = {"passed": False, "checks": None, "exit_code": None, "reason": "deadline-exceeded"}
        print(f"native HOL host: {'PASS' if hol_host['passed'] else 'FAIL'} ({hol_host['checks']} checks)")
    source_stable = all(digest(ROOT / name) == sha for name, sha in pins.items())
    binary_stable = digest(binary) == binary_before
    kernel_stable = all(digest(kernel / name) == sha for name, sha in kernel_pins.items())
    report = {"binary_sha256": binary_before, "binary_stable": binary_stable,
              "working_directory": str(working_dir),
              "source_stable": source_stable, "source_sha256": pins,
              "kernel_source_sha256": kernel_pins, "kernel_stable": kernel_stable, "outcomes": outcomes,
              "programming_assertions": sum(row["assertions"] for row in outcomes
                                             if row["program"] not in OPEN_BOUNDARIES),
              "open_boundary_assertions": sum(row["assertions"] for row in outcomes
                                               if row["program"] in OPEN_BOUNDARIES),
              "declaration_boundary_outputs": 8,
              "gap_probes": gap_probes, "native_hol_host": hol_host,
              "scope": "Executable language study; no new trusted rule or C refinement theorem"}
    if args.output_dir:
        (args.output_dir / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
    if not source_stable or not binary_stable or not kernel_stable:
        print("FAIL: a source or executable changed during qualification")
    return 0 if source_stable and binary_stable and kernel_stable \
        and all(row["passed"] for row in outcomes) \
        and (hol_host is None or hol_host["passed"]) else 1

if __name__ == "__main__":
    raise SystemExit(main())
