#!/usr/bin/env python3
"""Build the matching models, audit dependencies, and regenerate oracle corpora."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
PREFIX = "Mettapedia.Logic.LP."
CORPORA = {
    "DirectionalMatchingOracle": "pattern_model.json",
    "DirectionalMatchingSearchOracle": "pattern_search_model.json",
    "ClauseSubsumptionOracle": "pattern_clause_model.json",
}
MODELS = [
    "Matching", "MatchingControls", "RigidUnification",
    "RigidUnificationComposition", "RigidUnificationControls",
    "DirectionalMatching", "DirectionalMatchingScope",
    "DirectionalMatchingSearch", "VariantMatching", "VariantMatchingControls",
    "ClauseSubsumption", "ClauseSubsumptionControls",
    "ClauseSubsumptionEnumeration", "DirectionalMatchingAudit", *CORPORA,
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mettapedia", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    lean = args.mettapedia.resolve()
    sources = {}
    for name in MODELS:
        path = Path("Mettapedia/Logic/LP") / (name + ".lean")
        content = (lean / path).read_bytes()
        # The selected modules contain no permission to introduce proof holes.
        assert not re.search(rb"\b(sorry|admit|native_decide|theorem_wanted)\b", content), path
        assert not re.search(rb"^\s*axiom\s", content, re.M), path
        sources[str(path)] = hashlib.sha256(content).hexdigest()
    modules = [PREFIX + "DirectionalMatchingAudit", *(PREFIX + n for n in CORPORA)]
    env = dict(os.environ, NO_COLOR="1")
    build = subprocess.run(["lake", "build", *modules], cwd=lean, env=env,
                           capture_output=True, text=True, timeout=900)
    audit = build.stdout + build.stderr
    assert build.returncode == 0, audit
    assert "sorryAx" not in audit and "warning:" not in audit, audit
    groups = re.findall(r"depends on axioms:\s*\[([^]]*)\]", audit)
    assert groups, "No theorem dependency observations were produced"
    for group in groups:
        assert set(re.findall(r"[\w.]+", group)) <= {
            "propext", "Classical.choice", "Quot.sound"}, group
    checked = []
    for module, fixture in CORPORA.items():
        source = Path("Mettapedia/Logic/LP") / (module + ".lean")
        result = subprocess.run(["lake", "env", "lean", "--run", str(source)],
            cwd=lean, env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, (module, result.stdout, result.stderr)
        actual = json.loads(result.stdout)
        path = ROOT / "tests/fixtures" / fixture
        expected = json.loads(path.read_text())
        reference = expected.pop("reference", None)
        if reference is not None:
            assert reference["generator"] == PREFIX + module, reference
            for model, digest in reference["files"].items():
                assert hashlib.sha256((lean / model).read_bytes()).hexdigest() == digest, model
        assert actual == expected, (module, "checked model and native test corpus differ")
        checked.append({"module": PREFIX + module, "fixture": str(path.relative_to(ROOT)),
                        "fixture_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "cases": len(actual["cases"])})
    receipt = {"model_source_sha256": sources, "corpora": checked,
               "observed_axiom_lists": len(groups),
               "trusted_lean_axioms": ["propext", "Classical.choice", "Quot.sound"],
               "scope": "Executable first-order models; native C correspondence is separately tested."}
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"PASS: {len(MODELS)} model sources, dependency audit, and {len(checked)} regenerated corpora")


if __name__ == "__main__":
    main()
