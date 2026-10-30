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
                 ("ic-theory", step_file), ("ic-theory", theory / "IcTheory/Compression/Theorem51.lean"),
                 ("ic-theory", theory / "IcTheory/Compression/Theorem52.lean"),
                 ("ic-theory", theory / "IcTheory/Compression/Section4.lean"),
                 ("ic-theory", theory / "IcTheory/Compression/Theorem41.lean"),
                 ("ic-theory", theory / "IcTheory/Compression/Definition31.lean"),
                 ("ic-theory", theory / "IcTheory/Compression/BFeatures.lean"),
                 ("ic-theory", theory / "IcTheory/Compression/Feature.lean"),
                 ("ic-theory", theory / "IcTheory/Computability/Complexity.lean"),
                 ("ic-theory", theory / "IcTheory/Computability/PrefixComplexity.lean"),
                 ("ic-theory", theory / "IcTheory/Computability/UniversalMachine.lean"),
                 ("ic-theory", theory / "1908.03781v2.pdf")]
    shared = "lean/mettapedia/Mettapedia/"
    for name in ("Algorithms/Compression/ScopedGuidance.lean", "Algorithms/Compression/FairSearch.lean",
                 "Algorithms/Compression/RelationalPruning.lean",
                 "Algorithms/Compression/RelationalSourceExport.lean",
                 "Algorithms/Compression/PrototypeLearning.lean",
                 "Algorithms/Compression/PackedIntegerLiteral.lean",
                 "Algorithms/Compression/TensorSelection.lean",
                 "Algorithms/Compression/LayeredSource.lean",
                 "Algorithms/Compression/LibraryTransfer.lean",
                 "Machines/Cursor/ListValueEquality.lean",
                 "Machines/PrefixPortfolio.lean",
                 "Machines/PrefixMachine.lean",
                 "Machines/ResumablePrefixClock.lean",
                 "Machines/HierarchicalPrefixSearch.lean",
                 "Machines/HierarchicalPrefixLaws.lean",
                 "Machines/HierarchicalPrefixCost.lean",
                 "Machines/FiniteStackWorker.lean",
                 "Machines/HierarchicalSourceTime.lean",
                 "Computability/StackMachineComposition.lean",
                 "Computability/StackMachinePortTime.lean",
                 "Computability/StackMachineCompositionReflection.lean",
                 "Computability/StackMachineOutputRelay.lean",
                 "Computability/StackMachineContextCall.lean",
                 "Computability/FiniteSupportMachine.lean",
                 "Machines/FiniteCompiledMachine.lean",
                 "Machines/ConditionalSourceMachine.lean",
                 "Computability/KolmogorovComplexity/NaturalWordEncoding.lean",
                 "Computability/NaturalWordMachine.lean",
                 "Computability/FramedHeadCall.lean",
                 "Computability/FramedFieldAssembly.lean",
                 "Machines/ICSourceCalls.lean",
                 "Machines/ICConditionalCalls.lean",
                 "Computability/StackMachineCompositionTail.lean",
                 "Computability/WordDropLastMachine.lean",
                 "Computability/NaturalWordDecodingMachine.lean",
                 "Computability/FramedCompressionCheck.lean",
                 "Machines/ICPhysicalCheck.lean",
                 "Examples/Compression/PhysicalCheckControls.lean",
                 "Computability/BinaryCountdownMachine.lean",
                 "Machines/ICPreparedCheck.lean",
                 "Machines/ICSourceClock.lean",
                 "Machines/PreparedICSearch.lean",
                 "Examples/Compression/PreparedSourceControls.lean",
                 "Computability/E2PairMachine.lean",
                 "Machines/ICEntryCheck.lean",
                 "Examples/Compression/E2EntryControls.lean",
                 "Computability/FiniteBitStrings.lean",
                 "Computability/KolmogorovComplexity/FeatureRandomness.lean",
                 "Computability/KolmogorovComplexity/RandomnessLevelSearch.lean",
                 "Computability/KolmogorovComplexity/FeatureRandomnessConverse.lean",
                 "Computability/KolmogorovComplexity/FeatureInformation.lean",
                 "Computability/KolmogorovComplexity/ConditionalProgramModel.lean",
                 "Computability/KolmogorovComplexity/FramedProgramModel.lean",
                 "Computability/KolmogorovComplexity/FramedPrefixMachine.lean",
                 "Computability/KolmogorovComplexity/FeaturePredicates.lean",
                 "Computability/KolmogorovComplexity/FeatureBPredicates.lean",
                 "Computability/KolmogorovComplexity/FeatureCompressionRatio.lean",
                 "Computability/KolmogorovComplexity/BoundedFeature.lean",
                 "Computability/KolmogorovComplexity/FeatureResidualCap.lean",
                 "Computability/KolmogorovComplexity/FeatureBInformation.lean",
                 "Computability/KolmogorovComplexity/FeatureBDecomposition.lean",
                 "Computability/KolmogorovComplexity/FeatureBChain.lean",
                 "Computability/KolmogorovComplexity/FeatureResidualSearch.lean",
                 "Computability/KolmogorovComplexity/FeatureGlobalSearch.lean",
                 "Computability/KolmogorovComplexity/FeatureDyadicSearch.lean",
                 "Computability/KolmogorovComplexity/FeatureHierarchicalSearch.lean",
                 "Computability/KolmogorovComplexity/SelfDelimitingCode.lean",
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
                 "Examples/Compression/FeatureGlobalSearchControls.lean",
                 "Examples/Compression/FeatureDyadicSearchControls.lean",
                 "Examples/Compression/FeatureHierarchicalSearchControls.lean",
                 "Examples/Compression/FeatureMachineTimeControls.lean",
                 "Examples/Compression/FeatureSchemeControls.lean",
                 "Examples/Compression/LayeredSourceControls.lean",
                 "Examples/Compression/LibraryTransferControls.lean",
                 "Examples/Compression/ListValueEqualityControls.lean",
                 "Examples/Compression/RelationalPruningControls.lean",
                 "Examples/Compression/TensorSelectionControls.lean",
                 "Examples/Compression/FeatureRandomnessControls.lean",
                 "Examples/Compression/FeatureBControls.lean",
                 "Examples/Compression/InterpreterCorrespondenceControls.lean",
                 "Examples/Compression/FramedPrefixControls.lean"):
        inspected.append(("Mettapedia", mettapedia / shared / name))
    inspected.append(("NIK", Path(__file__).resolve()))
    inspected.append(("NIK", production / "tests/support/lean/ic_original_interpreter_bridge.lean"))
    inspected.append(("NIK", production / "tests/support/lean/ic_original_prefix_bridge.lean"))
    inspected.append(("NIK", production / "tests/support/check_ic_interpreter_bridge.py"))
    for relative in ("lib/petta/lib_ic_source_seeds.metta", "lib/petta/lib_source_context.metta",
                     "tests/support/check_ic_source_seeds.py", "tests/support/check_ic_family_experiment.py",
                     "tests/support/test_ic_family_experiment.py", "tests/fixtures/ic_source_families.json"):
        inspected.append(("NIK", production / relative))
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
        "conditional_qualification": {
            "entry_points": ["make test-tensor-where", "make test-ic-piecewise"],
            "algorithm": "finite shared-parameter conditional DAGs and complete one-operation operand-reference search",
            "families": ["Boolean decision composition", "piecewise affine regression", "cumulative drift change"],
            "observation": "complete NC2/TP1 cost against original masks; predictions on new numerical inputs",
            "reference": "independent recursive common-algorithm implementation; no WILLIAM discovery run",
            "arbitrary_tree_discovery_claim": False,
            "runtime_speedup_claim": False},
        "symbolic_application_obligations": ["family-separated-held-out-programs", "reviewable-library-export",
            "original-learned-prepared-and-bulk-cost-panel", "learning-and-preparation-break-even",
            "choice-effect-and-failure-preservation"],
        "frozen_source_family_experiment": {
            "entry_points": ["make test-ic-source-seeds", "make test-ic-source-families"],
            "manifest": "tests/fixtures/ic_source_families.json",
            "selection": "native structural ranking of a declared same-head seed region; native complete-cost acceptance on training sources only",
            "partition": "three corpus categories, disjoint declared program families, linked generated variants and whole-document renamed-clone refusal",
            "validation": "fixed definitions transferred without receiving-source fitting; independent reconstruction and complete bytes; ordered query cohorts and parameter consumer gate",
            "full_three_corpus_utility_claim": False,
            "execution_speedup_claim": False,
            "retained_heap_improvement_claim": False},
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
            "shared_b_feature_information": "IC Lemma 3.5: b-minimum incompressibility, both independence directions, joint information, source-conditioned feature descriptions and polynomial counting, with constants chosen before all b >= 1 and all sources",
            "shared_b_residual_cap": "the real ratio is exactly a finite natural cap; one actual extractor reads its charged self-delimiting binary encoding and filters finite checks before selecting a residual",
            "shared_b_decomposition": "minimum b-maps and their actual outputs give logarithmic pair and length partitions, without assuming unrestricted minima or a computable real-comparison oracle",
            "shared_b_chain_information": "actual shortest b-chains reconstruct, split at positions and satisfy uniform incremental and pairwise signed-information bounds; existing rational schemes instantiate them, and geometric iteration bounds give squared-logarithmic information overhead",
            "original_plain_interpreter_correspondence": "the actual original decoder instantiates the same shared interface source under its own toolchain; all program strings, outputs, divergence, plain minimum lengths and feature predicates correspond, with positive natural-factor b observations",
            "original_compiler_layout": "its actual exact-pair and sentinel layout proves constant-prefix universality in the shared sense, with complete prefix length charged; not an instruction-time claim",
            "original_prefix_program_correspondence": "actual original E1/E2 encoders and contextual readers give exact PrefixRuns observations, program-length sets and minimum prefix-coded description lengths on the shared framed model; the whole admitted domain is prefix-free",
            "shared_framed_prefix_machine": "a real canonical-frame parser and declared condition pairing induce an effective prefix machine; one fixed head relays every payload and condition of the simulated effective algorithm",
            "original_prefix_reference_comparison": "one constant precedes every output and condition: genuine reference complexity is at most framed complexity plus a constant; the reverse bound charges 2*log2(reference_complexity+1) plus a constant",
            "original_prefix_reference_identified_with_shared_reference": False,
            "different_plain_interpreters_minimum_programs_identified": False,
            "b_minimality_identified_with_unrestricted_minimality": False,
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
            "shared_tensor_selection": "checked coordinate selection, ordered support traversal, exact two-pass storage count and finite tree compilation versus path-following interpretation; not C or IEEE verification",
            "shortest_feature_hypotheses_supplied_by_practical_cost_decrease": False,
            "fair_search": "actual eight-turn cost/age queue, finite successor paths and explicit residual completion",
            "shared_global_feature_search": "one fixed partial-recursive interpreter code, enumerated program pairs, divergent competing trials, reconstruction-checked child searches and finite-prefix soundness; every finite valid compression chain is eventually observed",
            "shared_greedy_feature_search": "actual local cost/age search commits at its first verified publication; finite budgets reconstruct, resumption composes, and a valid nonempty chain forces a finite first commitment",
            "shared_dyadic_prefix_portfolio": "existing bitstring enumeration and Kraft inequality derive inclusive and strict phase allocations; workers retain actual configurations and exact phase-clock resumption",
            "shared_machine_step_search": "one fixed compiled finite-control four-stack autoencoder performs extraction, reconstruction and strict shortening; every valid one-step candidate is reached within 4*2^L*max(tau,1) allocated slots, using complete canonical pair length L and actual TM2 transition count tau",
            "shared_hierarchical_allocation": "single-turn inclusive dyadic clocks retain every running worker and child; all-branch soundness, source-length depth completeness, exact resumption and cumulative sum 2^(L_1+...+L_i+3*i)*(tau_i+1) for actual checked-worker times",
            "shared_hierarchical_debits": "at most one actual successful worker transition and source_length administrative selectors per root turn; debit totals compose under resume; variable-cost phase construction, initialization, decoding and description materialization are outside this count",
            "shared_physical_port_time": "fixed syntactic push/pop constants bound clean separated-port input/output lengths by actual source transitions; the in-place identity refutes this without distinct ports",
            "shared_physical_output_relay": "original execution, real two-pass handoff and physical copying; t+6*output_cells+4 bound, with explicit input charge for in-place sources; clean source halts give output reflection and clean relay halts",
            "shared_physical_context_call": "two physical argument passes retain the original context; original source steps are unchanged; ordered tagged argument/result packet within t+3*input_cells+2*output_cells+5 transitions; clean-halt output reflection and divergence preservation; not an IC reconstruction or shortening test",
            "shared_compiled_finite_source": "actual compiler codeSupp closes one finite label table before all inputs; original and packaged physical executions reflect in the same transition budget; encoded-input clean halts and context calls with explicit word-copy costs; no arbitrary-malformed-input or fast-compiler assumption",
            "shared_two_field_conditional_source": "one fixed source code exists before all program/condition strings for every effective conditional algorithm and exact word codec; two separate trList fields; canonical query reader, source soundness/completeness and context reflection; outer input encoding and full IC checking costs remain separate",
            "shared_physical_word_encoding": "the original exact dense natural-word codec has shared round-trip/primitive-recursive laws and explicit original-codec correspondences; real finite-borrow conversion produces a canonical compiler field within 2*word_bits+3 transitions; contextual suffixes are retained, and two-field input length is between total word bits+2 and total word bits+4; suffix placement and full IC checking remain separate",
            "shared_framed_head_call": "one unchanged source executes the first complete delimited field; every remaining cell is preserved, including empty fields and duplicate occurrences; counted source reflection and divergence preservation; t+2*head_cells+2*output_cells+2*tail_cells+6 physical transitions",
            "shared_dependent_conditional_calls": "fixed grouped field assembly calls the same source twice, using its first residual in the second query and retaining feature/residual/original metadata; two source times plus 128*(input_frame_cells+residual_cells+rebuilt_cells+1); one fixed plain universal source precedes all task words; dense codec overhead bounded by 128*(all five word lengths+14); output remains unchecked until equality and shortening stages",
            "shared_physical_word_decoding": "canonical dense natural fields are read, incremented and physically stripped of their high sentinel; original words are recovered within 24*(word_bits+1), and ordered framed vectors within 32*input_frame_cells+2; arbitrary malformed natural-field admission is not asserted",
            "shared_physical_checked_body": "one fixed partial conditional source performs both dependent calls; a physical decoder and finite-register comparison/unary-debit checker publish a Boolean and both candidate words; actual terminal traces imply both genuine source computations, and every completed worker readout agrees with the mathematical executePair outcome, including refusal; either divergent source invocation prevents publication; encoded-input bound t1+t2+256*(five word lengths+9); outer candidate parsing/preparation, normalized interpreter time and hierarchy administration remain separate",
            "shared_original_word_preparation": "the original three-word frame is physically encoded within 11*input_frame_cells+2 and handed to the checked body; combined bound t1+t2+288*(five word lengths+9); sound completed readout and admitted-input clean halts; serialized candidate-pair parsing remains outside this preparation",
            "shared_normalized_source_clock": "one real output-separated relay executes the original source and copies every output cell; admitted-query runs reflect original computations without source totality; finite push/pop constants bound prepared checking by a fixed source-dependent factor times normalized_Tg+normalized_Tf+1; normalized clocks are not identified with the original in-place interpreter clock",
            "shared_prepared_dyadic_source_bound": "after canonical pair admission, actual prepared workers have all-observation soundness and reach every valid step within 4*2^complete_pair_length*fixed_factor*(normalized_Tg+normalized_Tf+1) allocated slots; one normalized universal source precedes every task; admission/parsing, initial materialization, observer decoding and variable scheduler administration remain outside the slot clock",
            "shared_binary_countdown": "a finite-register in-place debit retains counter width and publishes a success/zero flag; borrow scan and restoration have actual machine counts; a proved zero-bit potential amortizes debit plus one payload transition, with other caller operations separate; every reachable counter configuration uses at most input_width+1 physical stack cells, not a native-heap measurement; the exact E2 header is big-endian and reverses when pushed into the little-endian counter",
            "shared_physical_e2_pair_parser": "the unchanged seven-bit count header and two canonical E2 fields are physically consumed from a two-field job; binary debits reuse one in-place arena, and flag/payload work is fused into one fixed statement; every terminal trace implies an exact canonical job, and ordered three-field output agrees with independent framing; malformed and extended inputs cannot become completed jobs; bound 8*(complete_job_cells+1), without unary expansion of the announced payload value",
            "shared_complete_entry_worker": "the physical E2 parser, original-word preparation and checker are joined within one worker; raw candidate/source input and every completed readout agree with the mathematical autoencoder, including refusal; invalid candidates cannot publish at any budget; original-source bound t1+t2+320*(complete_pair_length+source_bits+residual_bits+rebuilt_bits+9); fixed normalized-source entry factor yields 4*2^complete_pair_length*entry_factor*(normalized_Tg+normalized_Tf+1) retained slots; all finite emissions are sound; eligibility enumeration, initialization, observation and variable scheduler administration remain outside the slot clock",
            "shared_original_time_composition": "two counted source runs give a real composite run within (1+2*first_push_bound)*first_time+second_time+2 when the first machine has distinct ports; retained workers and live hierarchy use this original-time bound",
            "IC_specific_packet_context_checking_wrapper_completed_claim": True,
            "IC_specific_wrapper_scope": "actual two-field candidate/source job; normalized output-separated source clock; worker transitions, not full scheduler wall-clock time or native C verification",
            "hierarchical_allocation_is_full_interpreter_time_claim": False,
            "hierarchical_profile_is_printed_strict_pseudocode_claim": False,
            "shared_framed_pair_boundary": "canonical two-field framing, primitive-recursive encoder/decoder, exact lengths, extension refusal and an unbounded-overhead counterexample for arbitrary plain programs",
            "shared_feature_randomness_test": "IC Theorem 5.1: a uniform primitive-recursive lower approximation and exact 2^m * level-cardinality <= 2^n density over the shared partial interpreter; no ideal shortest-feature premise",
            "shared_randomness_level_correspondence": "the untruncated density bound is equivalent to the independent natural-power bound together with empty levels above source length; a negative control rejects the truncated bound alone",
            "shared_randomness_test_feature": "IC Theorem 5.2 on the concrete effective prefix host: one shared partial-recursive encoder and decoder, stable append-only level indices, fully charged self-delimiting padding, exact |feature| = threshold - 1 and explicit nonvacuity from unboundedness",
            "infinite_sequence_randomness_formalization_claim": False,
            "bounded_evaluator_stage_is_Turing_machine_time": False,
            "cost_age_policy_identified_with_paper_dyadic_ALICE": False,
            "remaining": ["underlying-interpreter-and-variable-administration-cost-join",
                          "strict-phase-operational-realization-and-quantitative-greedy-commitments"]},
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
