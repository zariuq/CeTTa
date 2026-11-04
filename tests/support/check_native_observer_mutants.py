#!/usr/bin/env python3
"""Qualify observer detection against ten reviewed archived mutant artifacts.

The input directory contains NAME/mutant for each profile below. Binaries are
read, never rebuilt or patched. --oracle-run is a completed full observer run
on the pinned repaired binary. Every mutant ordinary route must agree with
that oracle on identical generated source; timeouts are never mutant kills.
Unrelated failing oracle cases remain reported and do not become qualified
merely because this separate mutation-detection gate passes.
Three discrepancies are shrunk, then replayed against the repaired binary.
"""
import argparse
import json
from pathlib import Path
from types import SimpleNamespace

from check_native_observers import Run, compare, evaluate_case, save_json, sha256, shrink
from native_observer_cases import generate
from native_observer_probes import REVIEWED_SHA256S

PROFILES = {
    "scalar-order-loose": (("scalar-fold",), "99a39ed67ace0ab2dd78193e2ba18225be765cd6bfaf0339ce20249bccb9fc45"),
    "scalar-sum-wrapped": (("scalar-fold",), "bc9a5b97b288a204cf03de2c55596b4535cc39b55d42f76d14b5e075b64e45f8"),
    "filtered-unfiltered": (("filtered-match",), "fab934a00b8849bfd26051933016835d9fd48320d6c4ada19b609c992e5ed5f8"),
    "grouped-key-ignored": (("grouped-count",), "7ae2ced40a5324950c60a1a61e7add7e42ebbeda706aba1ef90404ed6bb9e478"),
    "grouped-absent-one": (("grouped-count",), "0ce4820d1b3093d7025ccb9d99b0ff58400e84177c01ccf4cf445a5bbf7f19a9"),
    "listed-rows-dropped": (("listed-match",), "662a0aae7c5383cbe5b046f94c5b96cac65afb9c7a75f890cce6106fe40ba34c"),
    "listed-bag-set": (("listed-match",), "01e10dc043b1675b6db75e29278b0d924ab14a451ef837dccd17ce08a572bb1b"),
    "frame-per-hole": (("open-join",), "d29c058dcf889331a7d4fafb3d70faae06b2f8d9164ffa3135c119d81a849e9f"),
    "guard-ignored": (("guard-batch",), "9d97ded9ebfd386bad72630715cddb8168203acbf923f6822d8d3b1b21f69176"),
    "dead-rule-any": (("dead-rule", "saturation"), "67a4506b7901345d6a5f26ed15e1c2de0e2bdf11c92b18ac424a25c4e0f6f9c6"),
}


def qualifies(cases):
    return (bool(cases) and any(c["verdict"].endswith("mismatch") for c in cases)
            and all(c["oracle_reference"] == "equal" and c["fixed_verdict"] == "equal"
                    and (c["verdict"] == "equal" or c["verdict"].endswith("mismatch")) for c in cases))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--mutants-root", type=Path, required=True)
    parser.add_argument("--oracle-run", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    identity = sha256(binary)
    if identity not in REVIEWED_SHA256S:
        raise SystemExit("no reviewed repaired-binary profile")
    mutants = args.mutants_root.resolve(strict=True)
    for name, (_, mutant_identity) in PROFILES.items():
        if sha256(mutants / name / "mutant") != mutant_identity:
            raise SystemExit("unknown mutant artifact: " + name)
    oracle = args.oracle_run.resolve(strict=True)
    summary = json.loads((oracle / "summary.json").read_text())
    if (summary["status"] not in ("qualified", "differences-or-inconclusive") or summary["binary_sha256"] != identity
            or not summary["binary_unchanged"] or not summary["harness_unchanged"]):
        raise SystemExit("oracle cohort has no stable completed receipts")
    support = Path(__file__).parent
    paths = [support / name for name in summary["source_files"]] + [Path(__file__)]
    identities = {p.name: sha256(p) for p in paths}
    if any(identities[name] != digest for name, digest in summary["source_files"].items()):
        raise SystemExit("oracle cohort used a different harness")
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    fixtures = generate(summary["seed"], summary["per_family"])
    options = SimpleNamespace(stats=False, timeout=15, output_limit=2*1024*1024, gdb_probe=False)
    results = []
    for name, (families, mutant_identity) in PROFILES.items():
        directory = root / name
        directory.mkdir()
        artifact = mutants / name / "mutant"
        selected = [case for case in fixtures if case.family in families]
        cases = []
        for case in selected:
            child = directory / f"case-{case.number:04d}"
            child.mkdir()
            outcome = evaluate_case(case, artifact, "eager", child, options)
            fixed_path = oracle / f"case-{case.number:04d}-eager" / "result.json"
            fixed = json.loads(fixed_path.read_text())
            source_same = outcome["source_sha256"] == fixed["source_sha256"] == sha256(fixed_path.parent / "program.metta")
            ordinary = compare(Run(**outcome["reference"]), Run(**fixed["reference"]), fixed["contracts"]) if source_same else "source-mismatch"
            cases.append({"case": case.number, "verdict": outcome["verdict"],
                          "fixed_verdict": fixed["verdict"], "oracle_reference": ordinary})
        passed = qualifies(cases)
        reduction = None
        if passed and name in ("grouped-absent-one", "frame-per-hole", "dead-rule-any"):
            failed = next(c for c in cases if c["verdict"].endswith("mismatch"))
            original = next(c for c in selected if c.number == failed["case"])
            shrinking = directory / "shrink"
            shrinking.mkdir()
            attempt = 0

            def evaluate(candidate):
                nonlocal attempt
                trial = shrinking / str(attempt)
                trial.mkdir()
                attempt += 1
                outcome = evaluate_case(candidate, artifact, "eager", trial, options)
                return failed["verdict"] if outcome["verdict"] == failed["verdict"] else None

            minimal, reduction = shrink(original, evaluate, 80)
            save_json(shrinking / "minimized.json", {"case": minimal.to_dict(), **reduction})
            replay = directory / "replay-fixed"
            replay.mkdir()
            repaired = evaluate_case(minimal, binary, "eager", replay, options)
            passed = passed and reduction["status"] == "local-fixed-point" and repaired["verdict"] == "equal"
        passed = passed and sha256(artifact) == mutant_identity
        result = {"mutant": name, "sha256": mutant_identity, "cases": cases, "reduction": reduction, "passed": passed}
        save_json(directory / "summary.json", result)
        results.append(result)
        print(name, "PASS" if passed else "FAIL", flush=True)
    stable = (sha256(binary) == identity and identities == {p.name: sha256(p) for p in paths}
              and all(sha256(mutants / name / "mutant") == digest for name, (_, digest) in PROFILES.items()))
    save_json(root / "summary.json", {"repaired_sha256": identity, "identities_unchanged": stable,
        "source_files": identities, "oracle_status": summary["status"],
        "oracle_unresolved_cases": [c for c in summary["cases"] if c["verdict"] != "equal"], "results": results})
    return 0 if stable and all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
