#!/usr/bin/env python3
"""Join all native-observer and S6 receipts into one reviewable handoff.

Inputs are completed output directories, three publication summaries and the
Lean foundation-manifest JSON. The same executable identity must govern every
runtime package. This checker reruns no workload and makes no whole-C claim.
"""
import argparse
from collections import Counter
import json
from pathlib import Path

from check_native_observers import ROOT, save_json, sha256

EXPECTED_MODULES = {
    "RelationColumnOwnership", "RelationHoldReceipts", "RelationIdReuse",
    "RelationBagTransfer", "RelationRootOwners", "RelationOwnershipCostBridge",
    "StableNumberCompaction", "RelationNumberingCorrespondence", "RelationTransferCoordinates",
    "RelationWordOwnership", "RelationOccurrenceBounds", "RelationBinaryUseBound",
    "RelationTransferPublication", "RelationCapturedRoots", "RelationSnapshotMetadata",
    "RelationProviderRegistry", "MatchExistenceAdmission", "MatchExistencePartition",
}
STANDARD_AXIOMS = {"propext", "Classical.choice", "Quot.sound"}


def load_summary(path):
    path = path.resolve(strict=True)
    return json.loads((path / "summary.json").read_text()) if path.is_dir() else json.loads(path.read_text())


def runtime_identity(summary):
    return summary.get("binary_sha256") or summary.get("repaired_sha256")


def support_sources_current(summary):
    identities = summary.get("source_files", summary.get("files", {}))
    support = Path(__file__).parent
    return bool(identities) and all((support / name).is_file() and sha256(support / name) == identity
                                    for name, identity in identities.items())


def inventory_sources_current(summary):
    return all((ROOT / route["source"]).is_file() and
               sha256(ROOT / route["source"]) == route["source_sha256"]
               for route in summary.get("routes", {}).values())


def observer_ok(summary):
    return (summary.get("status") == "qualified" and summary.get("binary_unchanged")
            and summary.get("harness_unchanged") and not summary.get("failures")
            and not summary.get("unconfirmed_routes") and not summary.get("unconfirmed_subroutes")
            and all(case.get("verdict") == "equal" for case in summary.get("cases", ())))


def simple_package_ok(summary):
    return summary.get("identities_unchanged") and all(item.get("passed") for item in summary.get("results", ()))


def publication_ok(summary):
    return (summary.get("binary_unchanged") and summary.get("source_unchanged")
            and summary.get("harness_unchanged") and all(item.get("passed") for item in summary.get("records", ())))


def mutants_ok(summary):
    return (summary.get("identities_unchanged") and summary.get("oracle_status") == "qualified"
            and not summary.get("oracle_unresolved_cases")
            and all(item.get("passed") for item in summary.get("results", ())))


def inventory_ok(summary):
    return (summary.get("binary_unchanged") and summary.get("route_count") == 14
            and all(route.get("qualified_inventory_entry") for route in summary.get("routes", {}).values()))


def formal_ok(manifests):
    names = {m["ownerModule"].rsplit(".", 1)[-1] for m in manifests}
    axioms = set()
    declarations = 0
    nonlogical = 0
    clean = names == EXPECTED_MODULES and len(manifests) == len(EXPECTED_MODULES)
    for manifest in manifests:
        declarations += manifest.get("checkedBodyDeclarationCount", 0)
        nonlogical += manifest.get("nonLogicalDeclarationCount", 0)
        for declaration in manifest.get("declarations", ()):
            clean = clean and declaration["declaration"]["kind"] != "axiom" and not declaration["containsAdmission"]
            axioms.update(item["name"] for item in declaration["transitiveAxioms"])
        clean = clean and all(item.get("runtimeCounterpart") for item in manifest.get("nonLogicalDeclarations", ()))
    return {"passed": bool(clean and axioms <= STANDARD_AXIOMS), "modules": len(manifests),
            "checked_bodies": declarations, "nonlogical": nonlogical, "axioms": sorted(axioms)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--observer", type=Path, required=True)
    parser.add_argument("--coordinate", type=Path, required=True)
    parser.add_argument("--readers", type=Path, required=True)
    parser.add_argument("--publication", type=Path, nargs=3, required=True)
    parser.add_argument("--mutants", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--formal-manifests", type=Path, required=True)
    parser.add_argument("--source-receipt", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    identity = sha256(binary)
    packages = {
        "observer": load_summary(args.observer), "coordinate": load_summary(args.coordinate),
        "readers": load_summary(args.readers), "mutants": load_summary(args.mutants),
        "inventory": load_summary(args.inventory),
    }
    publications = [load_summary(path) for path in args.publication]
    source_receipt = json.loads(args.source_receipt.resolve(strict=True).read_text())
    package_hashes = {name: runtime_identity(summary) for name, summary in packages.items()}
    package_hashes.update({"publication-" + str(index): runtime_identity(summary)
                           for index, summary in enumerate(publications)})
    identities_agree = all(value == identity for value in package_hashes.values())
    formal = formal_ok(json.loads(args.formal_manifests.resolve(strict=True).read_text()))
    dispositions = Counter(item["disposition"] for summary in publications for item in summary["records"])
    checks = {
        "binary_unchanged": sha256(binary) == identity,
        "identities_agree": identities_agree,
        "support_sources_current": (all(support_sources_current(packages[name])
                                        for name in ("observer", "coordinate", "readers", "mutants"))
                                    and all(support_sources_current(summary) for summary in publications)
                                    and support_sources_current(source_receipt)),
        "inventory_sources_current": inventory_sources_current(packages["inventory"]),
        "observer": observer_ok(packages["observer"]) and len(packages["observer"].get("cases", ())) == 852,
        "coordinate": simple_package_ok(packages["coordinate"]) and len(packages["coordinate"].get("results", ())) == 12,
        "readers": (simple_package_ok(packages["readers"]) and len(packages["readers"].get("results", ())) == 16
                    and packages["readers"].get("provider_failure", {}).get("passed")),
        "publication": (len(publications) == 3 and all(publication_ok(summary) for summary in publications)
                        and dispositions == {"equal": 13, "fatal": 3, "value-mismatch": 3}),
        "mutants": mutants_ok(packages["mutants"]) and len(packages["mutants"].get("results", ())) == 10,
        "inventory": inventory_ok(packages["inventory"]),
        "formal": formal["passed"],
        "source_receipt": (source_receipt.get("binary_sha256") == identity and
                           source_receipt.get("status") == "qualified-source-receipt" and
                           all(source_receipt.get("checks", {}).values())),
    }
    result = {"binary_sha256": identity, "package_hashes": package_hashes, "checks": checks,
              "formal": formal, "publication_dispositions": dict(dispositions),
              "source_receipt": source_receipt,
              "observer_comparisons": len(packages["observer"].get("cases", ())),
              "coordinate_scenarios": len(packages["coordinate"].get("results", ())),
              "reader_scenarios": len(packages["readers"].get("results", ())),
              "mutants": len(packages["mutants"].get("results", ())),
              "routes": packages["inventory"].get("route_count"),
              "status": "qualified-handoff" if all(checks.values()) else "incomplete-handoff",
              "boundary": "Finite hash-pinned execution evidence plus model-level contracts; no whole-C verification."}
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    save_json(out / "handoff.json", result)
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "qualified-handoff" else 1


if __name__ == "__main__":
    raise SystemExit(main())
