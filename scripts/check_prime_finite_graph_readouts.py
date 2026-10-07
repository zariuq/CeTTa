#!/usr/bin/env python3
"""Replay qualified native graph readings against independently checked solvers."""

import argparse
from copy import deepcopy
import json
from pathlib import Path
import re
import subprocess

from check_prime_common_set_profiles import digest, replay
from generate_prime_profile_catalogue import local_closure
from prime_material_qualification import json_inputs, native_inputs, unchanged


def checked(command, root):
    result = subprocess.run(command, cwd=root, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError("Graph qualification failed:\n" + result.stdout + result.stderr)
    return result.stdout


def boolean(value):
    if not isinstance(value, bool):
        raise ValueError("An expected graph decision is not Boolean")
    return "[True]" if value else "[False]"


def graph(raw):
    if not isinstance(raw, dict) or set(raw) != {"root", "rows"}:
        raise ValueError("Malformed exported graph")
    rows, root = raw["rows"], raw["root"]
    if not isinstance(root, int) or isinstance(root, bool) or not 0 <= root < len(rows):
        raise ValueError("Invalid exported root")
    for row in rows:
        if not isinstance(row, list) or any(not isinstance(child, int) or isinstance(child, bool)
                                            or not 0 <= child < len(rows) for child in row):
            raise ValueError("Invalid exported endpoint")
    return f"(graph {root} (" + " ".join("(" + " ".join(map(str, row)) + ")" for row in rows) + "))"


def required_sources(canonical):
    sources = {}
    for count in (1, 2):
        for mask in range(2 ** (count * count)):
            adjacency = [[child for child in range(count)
                          if mask & (1 << (parent * count + child))]
                         for parent in range(count)]
            for root in range(count):
                sources[f"vertices-{count}-mask-{mask}-root-{root}"] = {
                    "root": root, "rows": adjacency}
    sources.update({
        "repeated-authored-slot": {"root": 0, "rows": [[1, 1], []]},
        "distinct-empty-targets": {"root": 0, "rows": [[1, 2], [], []]},
        "scott-pair-with-unreachable-empty-s0": {"root": 0, "rows": [[0], [0, 1], []]},
        "scott-pair-with-unreachable-empty-s1": {"root": 1, "rows": [[0], [0, 1], []]},
        "two-binary-quines-versus-loop-binary": {"root": 0, "rows": [[0, 1], [0, 1], [2]]},
        "two-binary-quines-versus-loop-loop": {"root": 2, "rows": [[0, 1], [0, 1], [2]]},
    })
    if canonical:
        sources.update({
            "second-recount-root-binary": {"root": 0, "rows": [[1, 2], [], [], [4], []]},
            "second-recount-root-unary": {"root": 3, "rows": [[1, 2], [], [], [4], []]},
            "authored-child-not-global-origin": {"root": 2, "rows": [[], [], [1]]},
            "authored-member-order": {"root": 2, "rows": [[], [0], [1, 0]]},
            "first-empty-occurrence": {"root": 2, "rows": [[], [], [1, 0]]},
        })
    return sources


def validated_base_panel(rows):
    if not rows or rows[0].get("kind") != "manifest":
        raise ValueError("Missing checked Aczel/raw/Foundation manifest")
    manifest = rows[0]
    for field, required in {"bounded_presentations": 34, "control_presentations": 6,
                            "pair_cases": 1600}.items():
        if type(manifest.get(field)) is not int or manifest[field] != required:
            raise ValueError("Incomplete Aczel/raw/Foundation coverage: " + field)
    cases = rows[1:]
    if len(cases) != 1600:
        raise ValueError("Incomplete Aczel/raw/Foundation observations")
    sources = required_sources(False)
    pairs = {left + "-versus-" + right: (first, second)
             for left, first in sources.items() for right, second in sources.items()}
    seen = set()
    for case in cases:
        name = case.get("name")
        if case.get("kind") != "case" or name not in pairs or name in seen:
            raise ValueError("Repeated or absent Aczel/raw/Foundation ordered pair")
        seen.add(name)
        first, second = pairs[name]
        if (case["left_raw"] != first or case["right_raw"] != second or
                case["left"] != graph(first) or case["right"] != graph(second)):
            raise ValueError("Base pair does not use its declared sources")
        for field in ("aczel_equal", "aczel_member", "raw_unfolding_equal",
                      "left_eligible", "right_eligible"):
            boolean(case.get(field))
    return manifest, cases


def base_panel_refusals(rows):
    mutations = []
    missing = deepcopy(rows)
    missing.pop()
    mutations.append(("missing-base-ordered-pair", missing))
    duplicate = deepcopy(rows)
    duplicate[2] = deepcopy(duplicate[1])
    mutations.append(("duplicated-base-ordered-pair", duplicate))
    raw = deepcopy(rows)
    raw[1].pop("raw_unfolding_equal")
    mutations.append(("omitted-raw-unfolding-observation", raw))
    rooted = deepcopy(rows)
    rooted[1]["left_raw"]["root"] = 1
    mutations.append(("changed-base-rooted-matrix", rooted))
    reduced = deepcopy(rows)
    reduced[0]["pair_cases"] -= 1
    mutations.append(("reduced-self-reported-base-coverage", reduced))
    results = []
    for name, altered in mutations:
        try:
            validated_base_panel(altered)
        except ValueError as error:
            results.append({"name": name, "refused": True, "reason": str(error)})
        else:
            raise RuntimeError("Base graph qualification accepted altered evidence: " + name)
    return results


def validated_scott_panel(rows):
    if not rows or rows[0].get("kind") != "manifest":
        raise ValueError("Missing checked canonical Scott manifest")
    manifest = rows[0]
    for field, required in {"bounded_presentations": 34, "control_presentations": 11,
                            "pair_cases": 2025, "members_cases": 45,
                            "kernel_certificates": 4095, "stage_literal_certificates": 4095,
                            "checked_sources": 45, "checked_stages": 45,
                            "checked_flat_sources": 45, "checked_flat_edges": 45,
                            "flat_cardinal_certificates": 45}.items():
        if type(manifest.get(field)) is not int or manifest[field] != required:
            raise ValueError("Incomplete canonical Scott coverage: " + field)
    references = manifest.get("kernel_theorems")
    if (manifest.get("observer") != "scott-canonical" or
            manifest.get("changed_bit_refused") is not True or
            manifest.get("changed_root_refused") is not True or
            not isinstance(manifest.get("certificate_bridge"), str) or
            not manifest["certificate_bridge"].startswith("ProfileScottCollapseCases.") or
            not isinstance(references, list) or len(references) < 18 or
            any(not isinstance(name, str) or not name.startswith("Mettapedia.SetTheory.Profiles.")
                for name in references) or len(references) != len(set(references))):
        raise ValueError("Canonical Scott kernel certificates or refusal controls are absent")
    cases = [row for row in rows[1:] if row.get("kind") == "case"]
    members = [row for row in rows[1:] if row.get("kind") == "members"]
    if len(cases) != 2025 or len(members) != 45 or len(rows) != 2071:
        raise ValueError("Incomplete canonical Scott observations")
    sources = {}
    for member in members:
        name = member.get("name")
        if not isinstance(name, str) or name in sources:
            raise ValueError("Malformed or repeated canonical source")
        source = member["input_raw"]
        if graph(source) != member["input"]:
            raise ValueError("Canonical source does not match its retained data")
        returned = [graph(value) for value in member["expected_raw"]]
        roots = [value["root"] for value in member["expected_raw"]]
        row = source["rows"][source["root"]]
        if (returned != member["expected"] or roots != member["retained_roots"] or
                len(roots) != len(set(roots)) or any(value["rows"] != source["rows"]
                                                   for value in member["expected_raw"]) or
                any(root not in row for root in roots) or
                [row.index(root) for root in roots] != sorted(row.index(root) for root in roots)):
            raise ValueError("Canonical members lost their authored witnesses or order")
        sources[name] = source
    required = required_sources(True)
    if set(sources) != set(required) or any(sources[name] != value for name, value in required.items()):
        raise ValueError("Canonical Scott source enumeration omitted a required presentation")
    # The proof bridge visits authored premise occurrences, including repeated
    # targets. These certificates are not independent material observations.
    children = len(sources) * sum(len(source["rows"][source["root"]])
                                 for source in sources.values())
    extra_counts = {"checked_child_observations": children,
                    "finite_disjunction_certificates": children + 2025,
                    "source_children_certificates": 2025,
                    "flat_comparison_certificates": children + 2025,
                    "flat_rank_certificates": sum(len(source["rows"]) for source in sources.values())}
    for field, required_count in extra_counts.items():
        if type(manifest.get(field)) is not int or manifest[field] != required_count:
            raise ValueError("Incomplete canonical Scott bridge certificates: " + field)
    pairs = {left + "-versus-" + right: (first, second)
             for left, first in sources.items() for right, second in sources.items()}
    if len(pairs) != 2025:
        raise ValueError("Canonical comparison names are ambiguous")
    seen = set()
    for case in cases:
        name = case.get("name")
        if name not in pairs or name in seen:
            raise ValueError("Repeated or absent canonical ordered pair")
        seen.add(name)
        first, second = pairs[name]
        if (case["left_raw"] != first or case["right_raw"] != second or
                case["left"] != graph(first) or case["right"] != graph(second)):
            raise ValueError("Canonical pair does not use its declared sources")
        boolean(case["scott_equal"])
        boolean(case["scott_member"])
    return manifest, cases, members


def scott_panel_refusals(rows):
    """Reject altered coverage and provenance without consulting native output."""
    mutations = []
    missing_pair = deepcopy(rows)
    missing_pair.pop(next(index for index, row in enumerate(missing_pair) if row.get("kind") == "case"))
    mutations.append(("missing-ordered-pair", missing_pair))
    raw_credit = deepcopy(rows)
    raw_credit[0]["observer"] = "raw-unfolding"
    mutations.append(("raw-unfolding-is-not-canonical", raw_credit))
    unchecked = deepcopy(rows)
    unchecked[0]["kernel_certificates"] -= 1
    mutations.append(("missing-original-observer-certificate", unchecked))
    unrefused = deepcopy(rows)
    unrefused[0]["changed_bit_refused"] = False
    mutations.append(("missing-kernel-mismatch-control", unrefused))
    missing_source = deepcopy(rows)
    source = next(row for row in missing_source if row.get("name") == "vertices-2-mask-15-root-1")
    source["name"] = "omitted-rooted-matrix"
    mutations.append(("missing-rooted-matrix", missing_source))
    wrong_origin = deepcopy(rows)
    member = next(row for row in wrong_origin if row.get("name") == "authored-child-not-global-origin")
    member["retained_roots"] = [0]
    member["expected_raw"] = [{"root": 0, "rows": member["input_raw"]["rows"]}]
    member["expected"] = [graph(member["expected_raw"][0])]
    mutations.append(("unrelated-global-member-origin", wrong_origin))
    reordered = deepcopy(rows)
    member = next(row for row in reordered if row.get("name") == "authored-member-order")
    for field in ("retained_roots", "expected_raw", "expected"):
        member[field].reverse()
    mutations.append(("changed-authored-member-order", reordered))
    duplicate = deepcopy(rows)
    first, second = [index for index, row in enumerate(duplicate) if row.get("kind") == "case"][:2]
    duplicate[second] = deepcopy(duplicate[first])
    mutations.append(("duplicated-ordered-pair", duplicate))
    results = []
    for name, altered in mutations:
        try:
            validated_scott_panel(altered)
        except ValueError as error:
            results.append({"name": name, "refused": True, "reason": str(error)})
        else:
            raise RuntimeError("Canonical Scott qualification accepted altered evidence: " + name)
    return results


def validated_kernel_references(audit, names):
    """Check bodies and dependencies, rather than merely resolving citations."""
    declarations = {}
    summary = None
    for row in audit:
        if row.get("kind") == "module":
            for entry in row["manifest"]["declarations"]:
                declarations[entry["declaration"]["name"]] = entry
        elif row.get("kind") == "summary":
            summary = row
    if not summary or summary.get("containsAdmission") is not False:
        raise ValueError("A complete checked-dependency audit is missing")
    references = {}
    for name in names:
        entry = declarations.get(name)
        if not entry:
            raise ValueError("A graph theorem reference lacks its body audit: " + name)
        declaration = entry["declaration"]
        axioms = {dependency["name"] for dependency in entry["transitiveAxioms"]}
        if (entry["containsAdmission"] or not declaration["hasCheckedBody"] or
                declaration["unsafe"] or declaration["partial"] or
                not declaration["fullType"] or not axioms <= {"propext", "Quot.sound"}):
            raise ValueError("A graph reference is not a constructive checked body: " + name)
        references[name] = entry
    return references, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--lean-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    lean = args.lean_root.resolve()
    binary = args.binary.resolve()
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    exporter = lean / "scripts/export_profile_graph_readout_cases.lean"
    scott_exporter = lean / "scripts/export_profile_scott_collapse_cases.lean"
    audit_exporter = lean / "scripts/audit_profile_executable_material_contracts.lean"
    entries = sorted(set(imported for source in [exporter, scott_exporter, audit_exporter]
                         for imported in re.findall(r"^import\s+(Mettapedia\.[\w.]+)\s*$",
                                                    source.read_text(), re.M)))
    # Every local imported source and the pinned library environment is kept
    # fixed while the exporter is checked, executed and replayed.
    sources = {}
    for entry in entries:
        sources.update(local_closure(lean, entry))
    inputs = native_inputs(root, binary)
    inputs.update({path: digest(path) for path in sources.values()})
    for path in [exporter, scott_exporter, audit_exporter, lean / "lean-toolchain", lean / "lake-manifest.json",
                 Path(__file__).resolve(), root / "scripts/check_prime_common_set_profiles.py",
                 root / "scripts/generate_prime_profile_catalogue.py"]:
        inputs[path] = digest(path)
    checked(["lake", "build", *entries], lean)
    artifacts = {}
    for name in sources:
        path = lean / ".lake/build/lib/lean" / (name.replace(".", "/") + ".olean")
        artifacts[path] = digest(path)
    audit_output = checked(["lake", "env", "lean", "-DwarningAsError=true",
                            "--run", str(audit_exporter)], lean)
    (directory / "lean-owned-audit.jsonl").write_text(audit_output)
    audit_rows = [json.loads(line) for line in audit_output.splitlines()]
    output = checked(["lake", "env", "lean", "-DwarningAsError=true",
                      "--run", str(exporter)], lean)
    (directory / "lean-expected.jsonl").write_text(output)
    rows = [json.loads(line) for line in output.splitlines()]
    manifest, cases = validated_base_panel(rows)
    base_refusals = base_panel_refusals(rows)
    program, expected = [], []
    seen = set()
    for case in cases:
        if case.get("kind") != "case" or case["name"] in seen:
            raise ValueError("Malformed or repeated graph comparison")
        seen.add(case["name"])
        left, right = graph(case["left_raw"]), graph(case["right_raw"])
        if left != case["left"] or right != case["right"]:
            raise ValueError("Graph source does not match its retained data")
        for operation, field in [("equal", "aczel_equal"), ("member", "aczel_member")]:
            program.append(f"!(set:interpret finite-graph aczel ({operation} {left} {right}))")
            expected.append(boolean(case[field]))
        program.append(f"!(set:interpret finite-graph raw-unfolding (equal {left} {right}))")
        expected.append(boolean(case["raw_unfolding_equal"]))
        for value, field in [(left, "left_eligible"), (right, "right_eligible")]:
            program.append(f"!(set:interpret finite-graph foundation (eligible {value}))")
            expected.append(boolean(case[field]))
        if case["left_eligible"] and case["right_eligible"]:
            for operation, field in [("equal", "aczel_equal"), ("member", "aczel_member")]:
                program.append(f"!(set:interpret finite-graph foundation ({operation} {left} {right}))")
                expected.append(boolean(case[field]))
    routes = [("default", (), {}),
              ("reference", (), {"CETTA_PRIME_RELATIONAL_PLAN_REFERENCE": "1"}),
              ("fuel", ("--fuel", "10000000"), {})]
    results = []
    for name, route, environment in routes:
        results.append(replay(binary, root, directory, "checked-solvers-" + name,
                              "\n".join(program) + "\n", expected, route, environment))
    scott_output = checked(["lake", "env", "lean", "-DwarningAsError=true",
                            "--run", str(scott_exporter)], lean)
    (directory / "lean-scott-expected.jsonl").write_text(scott_output)
    scott_rows = [json.loads(line) for line in scott_output.splitlines()]
    scott_manifest, scott_cases, members_cases = validated_scott_panel(scott_rows)
    scott_refusals = scott_panel_refusals(scott_rows)
    references, audit_summary = validated_kernel_references(audit_rows,
        manifest["kernel_theorems"] + scott_manifest["kernel_theorems"])
    scott_program, scott_expected, scott_seen = [], [], set()
    for case in scott_cases:
        if case["name"] in scott_seen:
            raise ValueError("Repeated canonical Scott comparison")
        scott_seen.add(case["name"])
        left, right = graph(case["left_raw"]), graph(case["right_raw"])
        if left != case["left"] or right != case["right"]:
            raise ValueError("Canonical Scott source does not match its retained data")
        for operation, field in [("equal", "scott_equal"), ("member", "scott_member")]:
            scott_program.append(f"!(set:interpret finite-graph scott-canonical ({operation} {left} {right}))")
            scott_expected.append(boolean(case[field]))
    members_seen = set()
    for case in members_cases:
        if case["name"] in members_seen:
            raise ValueError("Repeated canonical Scott member reading")
        members_seen.add(case["name"])
        source = graph(case["input_raw"])
        returned = [graph(raw) for raw in case["expected_raw"]]
        if source != case["input"] or returned != case["expected"]:
            raise ValueError("Member source does not match its retained data")
        if (case["retained_roots"] != [raw["root"] for raw in case["expected_raw"]] or
                any(raw["rows"] != case["input_raw"]["rows"] for raw in case["expected_raw"])):
            raise ValueError("Member reading did not retain its original presentation")
        scott_program.append(f"!(set:interpret finite-graph scott-canonical (members {source}))")
        values = ["(FiniteGraphValue scott-canonical " + value + ")" for value in returned]
        scott_expected.append("[(" + " ".join(values) + ")]")
    for name, route, environment in routes:
        results.append(replay(binary, root, directory, "checked-canonical-scott-" + name,
                              "\n".join(scott_program) + "\n", scott_expected, route, environment))
    for relative in ["finite_graph_readouts", "observed_graph_readouts", "logical_equality_readouts",
                     "scott_canonical_readouts", "stored_graph_readouts", "logical_conversion_readouts",
                     "alias_carrier_refusals", "open_readout_boundaries"]:
        fixture = root / "tests/prime/set_profiles" / (relative + ".metta")
        expected_file = fixture.with_suffix(".expected")
        inputs[fixture] = digest(fixture)
        inputs[expected_file] = digest(expected_file)
        for name, route, environment in routes:
            results.append(replay(binary, root, directory, relative + "-" + name,
                                  fixture.read_text(), expected_file.read_text().splitlines(), route, environment))
    unchanged({**inputs, **artifacts})
    payload = {"format": "prime-finite-graph-readouts-qualified-v1", "binary_sha256": inputs[binary],
               "manifest": manifest, "pair_cases": len(cases), "base_panel_refusals": base_refusals,
               "scott_manifest": scott_manifest, "scott_pair_cases": len(scott_cases),
               "scott_members_cases": len(members_cases), "scott_panel_refusals": scott_refusals,
               "results": results, "kernel_references": references, "owned_audit_summary": audit_summary,
               "inputs": json_inputs(inputs), "artifacts": json_inputs(artifacts),
               "boundary": "Finite native replay against checked general solver definitions; not a proof of C implementation correctness or a complete anti-foundation theory."}
    (directory / "qualified.json").write_text(json.dumps(payload, indent=2) + "\n")
    print(json.dumps({"passed": len(results), "pair_cases": len(cases),
                      "scott_pair_cases": len(scott_cases), "scott_members_cases": len(members_cases),
                      "observations": sum(row["observations"] for row in results)}))


if __name__ == "__main__":
    main()
