"""Check the active tcalc ownership coverage against regenerated artifacts."""

import hashlib
import json
from pathlib import Path
import re
import sys


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


coverage_path, plan_path, export_path, cesta_source_path = map(Path, sys.argv[1:])
coverage = json.loads(coverage_path.read_text())
cesta_source = json.loads(cesta_source_path.read_text())
plan = plan_path.read_text()

assert coverage["format"] == "CestaTcalcOwnershipCoverageV1"
assert coverage["ownership_plan_version"] in plan
assert coverage["plan_sha256"] == digest(plan_path)
assert coverage["ordinary_c_sha256"] == digest(export_path)
assert coverage["vendor_cesta_source_sha256"] == cesta_source["source_sha256"]
assert coverage["functions"] == len(re.findall(r"\n \(function ", plan))
assert coverage["foreign_obligations"] == len(re.findall(r"\n \(foreign ", plan))
assert coverage["unchecked_functions"] == len(re.findall(r"\n \(unchecked ", plan)) == 0
assert coverage["module_admitted"] is True
assert coverage["altered_plans_refused"] > coverage["functions"]
assert coverage["lean_is_a_development_dependency_only"] is True

print("PASS tcalc ownership coverage: "
      f"{coverage['functions']}/{coverage['functions']} functions, "
      f"{coverage['altered_plans_refused']} altered plans refused, no unchecked functions")
