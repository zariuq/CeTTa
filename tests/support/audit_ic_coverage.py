#!/usr/bin/env python3
"""Inventory the inspected WILLIAM task region and IC theorem boundaries.

This source audit never counts a declared or skipped test as a successful
learning run. Reference code is read as syntax and is not imported or executed.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
from pathlib import Path
import subprocess


TASK_FAMILIES = {
    "simple_repeat": "repetition",
    "insert_repeat": "concatenation-and-repetition",
    "insert_repeat2": "concatenation-and-repetition",
    "insert_repeat3": "concatenation-and-repetition",
    "repeat_with_noise": "noisy-sequences-and-insertion",
    "simply_linear": "ranges-and-affine-regression",
    "sprinkled": "sparse-outliers",
    "increasing_runs": "higher-order-sequence-composition",
    "map_negate": "higher-order-sequence-composition",
    "reg_only_y": "affine-regression",
    "reg_simple": "affine-regression",
    "reg_drift_change": "drift-and-cumulative-operations",
    "reg_multiple_vars": "multivariable-regression",
    "reg_matrix_linear": "matrix-regression",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def named(call, name):
    return isinstance(call, ast.Call) and isinstance(call.func, ast.Name) and call.func.id == name


def source_tasks(path):
    tree = ast.parse(path.read_text(), filename=path.name)
    build = next(node for node in tree.body
                 if isinstance(node, ast.FunctionDef) and node.name == "_build_all_domains")
    found = {}
    for node in ast.walk(build):
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "seq_params"
                                                for t in node.targets):
            for entry in node.value.elts:
                name = ast.literal_eval(entry.elts[1])
                found[name] = {"line": entry.lineno, "reference_region": "sequence_tasks"}
        if named(node, "CompressionTask"):
            for keyword in node.keywords:
                if keyword.arg == "name" and isinstance(keyword.value, ast.Constant):
                    name = keyword.value.value
                    found[name] = {"line": node.lineno, "reference_region":
                                   "matrix_tasks" if name == "reg_matrix_linear" else "regression_tasks"}
    if set(found) != set(TASK_FAMILIES):
        raise AssertionError({"missing": sorted(set(TASK_FAMILIES) - set(found)),
                              "unexpected": sorted(set(found) - set(TASK_FAMILIES))})
    return [{"name": name, "family": TASK_FAMILIES[name], **location,
             "reference_discovery_run": "not-run-by-this-audit",
             "cetta_discovery_qualification": "required"} for name, location in found.items()]


def skipped_discovery(path):
    tree = ast.parse(path.read_text(), filename=path.name)
    result = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.FunctionDef) or "clustering" not in node.name:
            continue
        skipped = any(isinstance(decorator, ast.Attribute) and decorator.attr == "skip"
                      and isinstance(decorator.value, ast.Attribute) and decorator.value.attr == "mark"
                      and isinstance(decorator.value.value, ast.Name) and decorator.value.value.id == "pytest"
                      for decorator in node.decorator_list)
        if skipped:
            result.append({"name": node.name, "line": node.lineno,
                           "reference_status": "unconditionally-skipped",
                           "counts_as_passed": False,
                           "cetta_discovery_qualification": "required"})
    if {row["name"] for row in result} != {"test_clustering_with_solution", "test_clustering_without_solution"}:
        raise AssertionError("clustering source region changed; inspect before updating the ledger")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--william", required=True, type=Path)
    parser.add_argument("--ic-theory", required=True, type=Path)
    parser.add_argument("--mettapedia", required=True, type=Path,
                        help="Mettapedia repository root")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    william, theory, mettapedia = (path.resolve(strict=True)
                                  for path in (args.william, args.ic_theory, args.mettapedia))
    tasks_file = william / "william/tests/test_alice.py"
    pipeline_file = william / "william/tests/test_alice_pipeline.py"
    log_file = theory / "IcTheory/Basics/LogBounds.lean"
    step_file = theory / "IcTheory/Compression/Theorem36.lean"
    log_source, step_source = log_file.read_text(), step_file.read_text()
    if "def LogLe (a b n : Nat)" not in log_source or "∃ c d : Nat" not in log_source:
        raise AssertionError("inspect the reference logarithmic comparison")
    if "def StepLogLe (a b s n : Nat)" not in step_source or "theorem theorem36_paper" not in step_source:
        raise AssertionError("inspect the reference incremental bound")
    production = Path(__file__).resolve().parents[2]
    roots = {"william": william, "ic-theory": theory, "Mettapedia": mettapedia, "NIK": production}
    inspected = [("william", tasks_file), ("william", pipeline_file), ("ic-theory", log_file),
                 ("ic-theory", step_file), ("ic-theory", theory / "1908.03781v2.pdf")]
    shared = "lean/mettapedia/Mettapedia/"
    for name in ("Algorithms/Compression/ScopedGuidance.lean", "Algorithms/Compression/FairSearch.lean",
                 "Algorithms/Compression/RelationalPruning.lean",
                 "Algorithms/Compression/RelationalSourceExport.lean",
                 "Algorithms/Compression/PrototypeLearning.lean",
                 "Algorithms/Compression/PackedIntegerLiteral.lean",
                 "Algorithms/Compression/LayeredSource.lean",
                 "Algorithms/Compression/LibraryTransfer.lean",
                 "Machines/Cursor/ListValueEquality.lean",
                 "Computability/KolmogorovComplexity/FeatureInformation.lean",
                 "Computability/KolmogorovComplexity/FeatureCompressionRatio.lean",
                 "Computability/KolmogorovComplexity/BoundedFeature.lean",
                 "Computability/KolmogorovComplexity/FeatureResidualSearch.lean",
                 "Computability/KolmogorovComplexity/FeatureShortMaps.lean",
                 "Computability/KolmogorovComplexity/FeatureConstrainedSearch.lean",
                 "Computability/KolmogorovComplexity/FeatureCompressionChain.lean",
                 "Computability/KolmogorovComplexity/ConditionalPlainComposition.lean",
                 "Computability/KolmogorovComplexity/FeatureSchemeCodec.lean",
                 "Computability/KolmogorovComplexity/FeatureRationalScheme.lean",
                 "Computability/KolmogorovComplexity/FeatureSchemePrefix.lean",
                 "Computability/KolmogorovComplexity/FeatureSourceInformation.lean",
                 "Computability/KolmogorovComplexity/FeatureDecompositionInformation.lean",
                 "Computability/KolmogorovComplexity/FeatureJointInformation.lean",
                 "Computability/KolmogorovComplexity/FeatureIncrementalInformation.lean",
                 "Computability/KolmogorovComplexity/ConditionalInformationTransport.lean",
                 "Computability/KolmogorovComplexity/FeatureOrthogonality.lean",
                 "Examples/Compression/FeatureOrthogonalityControls.lean",
                 "Examples/Compression/FeatureShortMapsControls.lean",
                 "Examples/Compression/FeatureConstrainedSearchControls.lean",
                 "Examples/Compression/FeatureSchemeControls.lean",
                 "Examples/Compression/LayeredSourceControls.lean",
                 "Examples/Compression/LibraryTransferControls.lean",
                 "Examples/Compression/ListValueEqualityControls.lean",
                 "Examples/Compression/RelationalPruningControls.lean"):
        inspected.append(("Mettapedia", mettapedia / shared / name))
    inspected.append(("NIK", Path(__file__).resolve()))
    result = {
        "kind": "IC-source-capability-ledger",
        "completion_claim": False,
        "reference_tasks": source_tasks(tasks_file),
        "clustering_discovery": skipped_discovery(pipeline_file),
        "additional_required_families": ["centralization", "comparisons", "masks", "partitions",
            "classification", "decision-tree-composition", "clustering", "mixed-integer-float-fitting",
            "prediction-on-new-inputs"],
        "numerical_obligations": ["replay-existing-witnesses", "bounded-native-discovery",
            "precision-and-original-task-validation", "explicit-missing-masks", "packed-payload-cost",
            "dimension-overflow-empty-and-duplicate-controls", "independent-common-algorithm-comparison"],
        "prototype_qualification": {
            "entry_point": "make test-ic-clustering",
            "algorithm": "finite farthest-first starts and bounded masked Lloyd updates",
            "model": "ordered NC2 target, TP1 GatherRows, charged centroid matrix and exact compact integer assignments",
            "reference": "independent common-algorithm implementation; no WILLIAM discovery run",
            "global_optimality_claim": False,
            "full_WILLIAM_clustering_coverage_claim": False},
        "symbolic_application_obligations": ["family-separated-held-out-programs", "reviewable-library-export",
            "original-learned-prepared-and-bulk-cost-panel", "learning-and-preparation-break-even",
            "choice-effect-and-failure-preservation"],
        "theory_boundaries": {
            "reference_LogLe": "constants quantified after the scalar quantities",
            "reference_StepLogLe": "constants quantified after the scalar quantities and chain length",
            "required_incremental_bound": "one overhead pair selected before the source family and chain",
            "reference_pointwise_theorems_alone_prove_uniform_bound": False,
            "shared_feature_bounds": "uniform compiler constants over the named shared interpreters",
            "shared_shortest_map_search": "partial-recursive residual search, reconstruction and logarithmic description bound",
            "shared_short_feature_maps": "plain-source-interpreter counterpart of IC Theorem 3.8: one compiled extractor, map length at most feature length plus a fixed overhead, and constant selected-residual information for bounded features",
            "shared_ratio_map_search": "effective guards filter each finite candidate before search; fixed rational factors retain ratio-restricted minima and constant short-map bounds",
            "shared_ratio_iteration_bound": "geometric shrinkage and logarithmic successful-step count for fixed real factors greater than one, including empty terminal residuals",
            "arbitrary_real_ratio_comparison_assumed_computable": False,
            "shared_complete_scheme_codec": "actual count and feature frames, raw terminal residual, total primitive-recursive decoder, partial-recursive interpreter and exact complete length",
            "shared_rational_scheme": "finite ideal schemes exist for every source; fixed rational factors give uniform bounded features, transported residual complexity and two-sided complete description bounds",
            "shared_scheme_prefix_bridge": "exact E2 framing, effective prefix interpreter, refusal of extensions and fully charged two-sided prefix-reference bounds; source feature minima remain plain-machine-relative",
            "plain_feature_minima_identified_with_prefix_feature_minima": False,
            "shared_incremental_information": "one overhead fixed before the source and complete shortest chain",
            "shared_information_transport": "effective compiler composition, signed symmetry and uniform IC Lemma 3.4",
            "shared_orthogonality": "IC Theorem 3.5: absolute signed information bounded by index distance times a uniform logarithmic budget",
            "shared_relational_pruning": "necessary chain/choice failure guards preserve dependent completion lists, ordered folds and common complete-cost selection",
            "shared_relational_source_export": "independent MIL and value-entry source IRs agree for references, identity, single-evaluation chain, occurrence-preserving alternatives and Cartesian map; fresh generated locals avoid reserved prefixes",
            "native_source_model_export": "checked MP1 examples/costs precede lexical ordinary-source emission; source and text costs include names, and fresh-process gates remain separate evidence",
            "shared_layered_source": "LM1 transport round trip and full length, ordered partial expansion, bounded Kahn agreement and checked helper-graph acyclicity; source execution remains a separate correspondence",
            "shared_fixed_library_transfer": "fixed definition-stage recovery, complete decoder validation, reverse installation and receiving-source reconstruction from local inverses; complete LM1 acceptance decreases serialized bits",
            "shared_list_value_equality": "independent structural and reading comparisons agree after closed-cell normalization, including cells nested inside records; opaque/numerical/native boundaries remain separate",
            "shortest_feature_hypotheses_supplied_by_practical_cost_decrease": False,
            "fair_search": "actual eight-turn cost/age queue, finite successor paths and explicit residual completion",
            "remaining": ["machine-correspondences-to-independent-development", "remaining-b-compression-results",
                "ALICE-and-Greedy-ALICE-global-interleaving", "randomness-test-bridges"]},
        "source_sha256": {label + "/" + str(path.relative_to(roots[label])): sha(path)
                          for label, path in inspected if path.suffix != ".lean"},
        "lean_source_paths": [label + "/" + str(path.relative_to(roots[label]))
                              for label, path in inspected if path.suffix == ".lean"],
        "lean_qualification": "requires-separate-build-and-axiom-audit",
        "reference_heads": {},
        "reference_has_local_changes": {},
    }
    for label, path in roots.items():
        head = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True, check=True)
        result["reference_heads"][label] = head.stdout.strip()
        status = subprocess.run(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=normal"],
                                capture_output=True, text=True, check=True)
        result["reference_has_local_changes"][label] = bool(status.stdout)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(json.dumps({"active_task_declarations": len(result["reference_tasks"]),
                      "skipped_clustering_discovery_tests": len(result["clustering_discovery"]),
                      "completion_claim": False, "ledger_sha256": sha(args.output)}))


if __name__ == "__main__":
    main()
