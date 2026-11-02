#!/usr/bin/env python3
"""Check the original IC interpreter against the shared Lean program interface.

Only the named original target and shared targets are built. Original package
caches are reused in place; shared interface objects for the original compiler
are ordinary qualification outputs. Lean sources are never fingerprinted.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess


INTERFACES = ("ConditionalProgramModel", "FeaturePredicates", "FeatureBPredicates",
              "FramedProgramModel", "NaturalWordEncoding")
SHARED_TARGETS = (
    "Mettapedia.Examples.Compression.InterpreterCorrespondenceControls",
    "Mettapedia.Examples.Compression.FramedPrefixControls",
    "Mettapedia.Examples.Compression.FeatureBControls",
    "Mettapedia.Examples.Compression.FeatureRandomnessControls",
    "Mettapedia.Examples.Compression.FeatureDyadicSearchControls",
    "Mettapedia.Examples.Compression.FeatureHierarchicalSearchControls",
    "Mettapedia.Examples.Compression.FeatureMachineTimeControls",
    "Mettapedia.Examples.Compression.FeatureGlobalSearchControls",
    "Mettapedia.Examples.Compression.FeatureSchemeControls",
)
ALLOWED_AXIOMS = {"propext", "Classical.choice", "Quot.sound"}
ENTRY_WORKER_THEOREMS = (
    "Mettapedia.Computability.E2PairMachine.terminal_job",
    "Mettapedia.Computability.E2PairMachine.reflects_outputs",
    "Mettapedia.Computability.E2PairMachine.clean_halts",
    "Mettapedia.Machines.ICEntryCheck.sound",
    "Mettapedia.Machines.ICEntryCheck.clean_halts_at",
    "Mettapedia.Machines.ICEntryCheck.no_publication_if_candidate_invalid",
    "Mettapedia.Machines.PreparedICSearch.entry_emitted_reconstructs",
    "Mettapedia.Machines.PreparedICSearch.entry_retained_from_source_clocks",
    "Mettapedia.Machines.PreparedICSearch.fixed_universal_entry_one_step",
)


def checked(command, cwd, env=None):
    return subprocess.run(command, cwd=cwd, env=env, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()


def run_logged(command, cwd, log, env=None, *, reference_warnings=False):
    with log.open("w") as stream:
        completed = subprocess.run(command, cwd=cwd, env=env, stdout=stream,
                                   stderr=subprocess.STDOUT, check=False)
    if completed.returncode:
        raise RuntimeError(f"qualification command failed ({completed.returncode}); inspect {log}")
    output = log.read_text()
    if re.search(r"\berror:", output) or (not reference_warnings and re.search(r"\bwarning:", output)):
        raise RuntimeError(f"qualification emitted a warning or error; inspect {log}")
    return output


def source_closure(root, first, namespace):
    pending, sources = list(first), {}
    while pending:
        module = pending.pop()
        relative = Path(module.replace(".", "/") + ".lean")
        if relative in sources:
            continue
        path = root / relative
        source = path.read_bytes()
        sources[relative] = source
        for match in re.finditer(rb"(?m)^(?:public\s+)?import\s+([^\n]+)", source):
            pending.extend(name for name in match[1].decode().split()
                           if name.startswith(namespace + "."))
    return sources


def audit_records(output, key, *, required=()):
    records = []
    for line in output.splitlines():
        start = line.find('{"')
        if start < 0:
            continue
        record = json.loads(line[start:])
        if key in record:
            records.append(record)
    if len(records) != 1:
        raise AssertionError(f"expected one {key} audit, got {len(records)}")
    record = records[0]
    declarations = record.get("auditedDeclarations", record.get("auditedTheorems", []))
    if not declarations:
        raise AssertionError("empty declaration audit")
    if type(record[key]) is not int or record[key] < len(declarations):
        raise AssertionError("invalid declaration count")
    if "auditedTheorems" in record and (type(record.get("theorems")) is not int
                                        or record["theorems"] != len(declarations)):
        raise AssertionError("incomplete theorem audit")
    if key in {"originalInterpreterBridgeDeclarations", "originalPrefixBridgeDeclarations"} and record[key] != len(declarations):
        raise AssertionError("incomplete original declaration audit")
    names = [declaration["name"] for declaration in declarations]
    if len(set(names)) != len(names):
        raise AssertionError("duplicate audited declaration")
    missing = set(required) - set(names)
    if missing:
        raise AssertionError(f"missing required theorem evidence: {', '.join(sorted(missing))}")
    for declaration in declarations:
        if not set(declaration["axioms"]) <= ALLOWED_AXIOMS:
            raise AssertionError(f"unpermitted axiom dependency in {declaration['name']}")
    if "separateCompilerImplementations" in record:
        implementations = record.get("compilerImplementations", [])
        if (type(record["separateCompilerImplementations"]) is not int
                or record["separateCompilerImplementations"] != len(implementations)):
            raise AssertionError("invalid compiler implementation count")
        names = [item["name"] for item in implementations]
        if len(set(names)) != len(names):
            raise AssertionError("duplicate compiler implementation")
        if any(item["name"] != item["logicalOwner"] + "._unsafe_rec" for item in implementations):
            raise AssertionError("compiler implementation has no matching logical owner")
        if any(item.get("matchingTypeLevelsAndOwner") is not True for item in implementations):
            raise AssertionError("compiler implementation provenance was not checked")
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ic-theory", required=True, type=Path)
    parser.add_argument("--mettapedia", required=True, type=Path)
    parser.add_argument("--original-toolchain", required=True, type=Path,
                        help="installed matching toolchain directory containing bin/lean and bin/lake")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    original = args.ic_theory.resolve(strict=True)
    mettapedia = args.mettapedia.resolve(strict=True)
    shared = mettapedia / "lean/mettapedia"
    toolchain = args.original_toolchain.resolve(strict=True)
    lean, lake = toolchain / "bin/lean", toolchain / "bin/lake"
    production = Path(__file__).resolve().parents[2]
    client = production / "tests/support/lean/ic_original_interpreter_bridge.lean"
    prefix_client = production / "tests/support/lean/ic_original_prefix_bridge.lean"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    pin = (original / "lean-toolchain").read_text().strip()
    version = checked([str(lean), "--version"], original)
    if pin.rsplit(":v", 1)[-1] not in version:
        raise AssertionError("the original compiler does not match its declared toolchain")
    if not (original / ".lake").is_dir() or not (shared / ".lake").is_dir():
        raise AssertionError("both existing project caches are required")
    original_targets = ("IcTheory.Compression.Definition31", "IcTheory.Computability.PrefixComplexity")
    originals = source_closure(original, original_targets, "IcTheory")
    shared_sources = source_closure(shared, SHARED_TARGETS, "Mettapedia")
    gate_sources = {path: path.read_bytes() for path in
                    (client, prefix_client, Path(__file__).resolve(), Path(__file__).with_name("test_ic_interpreter_bridge.py"),
                     production / "Makefile")}
    pins_before = {(root, name): (root / name).read_bytes()
                   for root in (original, shared) for name in ("lean-toolchain", "lake-manifest.json")}
    reference_output = run_logged([str(lake), "build", *original_targets], original,
                                  output / "original-reference-build.log", reference_warnings=True)
    reference_warnings = [line for line in reference_output.splitlines() if re.search(r"\bwarning:", line)]
    run_logged(["lake", "--wfail", "build", *SHARED_TARGETS], shared, output / "shared-build.log")
    environment = os.environ.copy()
    search = checked([str(lake), "env", "printenv", "LEAN_PATH"], original)
    artifacts = output / "lean"
    environment["LEAN_PATH"] = str(artifacts) + os.pathsep + search
    interface_logs = []
    for module in INTERFACES:
        relative = Path("Mettapedia/Computability/KolmogorovComplexity") / (module + ".lean")
        destination = (artifacts / relative).with_suffix(".olean")
        destination.parent.mkdir(parents=True, exist_ok=True)
        log = output / (module + ".log")
        run_logged([str(lean), "-DwarningAsError=true", "-R", str(shared), str(shared / relative),
                    "-o", str(destination)], original, log, environment)
        interface_logs.append(str(log.relative_to(output)))
    plain_object = (artifacts / client.relative_to(production)).with_suffix(".olean")
    plain_object.parent.mkdir(parents=True, exist_ok=True)
    bridge_output = run_logged([str(lean), "-DwarningAsError=true", "-R", str(production), str(client),
                                "-o", str(plain_object)],
                               original, output / "original-bridge.log", environment)
    prefix_output = run_logged([str(lean), "-DwarningAsError=true", "-R", str(production), str(prefix_client)],
                               original, output / "original-prefix-bridge.log", environment)
    original_audit = audit_records(bridge_output, "originalInterpreterBridgeDeclarations")
    prefix_audit = audit_records(prefix_output, "originalPrefixBridgeDeclarations")
    shared_audit = audit_records((output / "shared-build.log").read_text(),
                                 "sharedInterpreterInterfaceDeclarations")
    framed_audit = audit_records((output / "shared-build.log").read_text(), "framedPrefixDeclarations")
    hierarchical_audit = audit_records((output / "shared-build.log").read_text(), "hierarchicalSearchDeclarations")
    source_time_audit = audit_records((output / "shared-build.log").read_text(), "sourceMachineTimeDeclarations",
                                      required=ENTRY_WORKER_THEOREMS)
    for root, captured in ((original, originals), (shared, shared_sources)):
        if any((root / relative).read_bytes() != source for relative, source in captured.items()):
            raise AssertionError("source dependencies changed during qualification")
    if any(path.read_bytes() != source for path, source in gate_sources.items()):
        raise AssertionError("a bridge client or qualification driver changed during qualification")
    if any((root / name).read_bytes() != source for (root, name), source in pins_before.items()):
        raise AssertionError("a toolchain pin or package manifest changed during qualification")
    result = {
        "kind": "IC-original-interpreter-correspondence", "passed": True,
        "original_compiler": version, "original_toolchain_pin": pin,
        "shared_compiler": checked(["lake", "env", "lean", "--version"], shared),
        "original_head": checked(["git", "rev-parse", "HEAD"], original),
        "shared_head": checked(["git", "rev-parse", "HEAD"], mettapedia),
        "original_Lean_source_paths": [str(path) for path in sorted(originals)],
        "shared_Lean_source_paths": [str(path) for path in sorted(shared_sources)],
        "bridge_client": str(client.relative_to(production)),
        "prefix_bridge_client": str(prefix_client.relative_to(production)),
        "interface_sources_compiled_by_both_toolchains": list(INTERFACES),
        "interface_logs": interface_logs, "original_audit": original_audit, "shared_audit": shared_audit,
        "original_prefix_audit": prefix_audit, "framed_prefix_audit": framed_audit,
        "hierarchical_search_audit": hierarchical_audit,
        "source_machine_time_audit": source_time_audit,
        "source_bytes_stable": True, "pins_and_manifests_unchanged": True,
        "original_reference_warning_count": len(reference_warnings),
        "original_reference_warnings": reference_warnings,
        "shared_and_bridge_warning_count": 0,
        "lean_source_hashes_recorded": False, "cache_copies_created": False,
        "scope": "all original program strings and observations; exact plain and prefix-coded minimum lengths; original complete prefix domain and actual encoders; positive natural b factors; concrete compiler layout and charged logarithmic payload framing; retained inclusive hierarchical allocation with the actual compiled checker",
        "does_not_identify_other_interpreters_minimum_programs": True,
        "prefix_program_correspondence_claim": True,
        "prefix_reference_comparison": "constant lower comparison and charged logarithmic upper comparison",
        "original_prefix_machine_is_reference_claim": False,
        "hierarchical_machine_time_correspondence_claim": False,
        "hierarchical_allocation_bound_claim": True,
        "source_machine_port_time_and_composition_claim": True,
        "source_machine_context_call_claim": True,
        "compiled_machine_finite_restriction_claim": True,
        "fixed_two_field_conditional_source_claim": True,
        "exact_dense_word_codec_correspondence_claim": True,
        "physical_word_encoding_linear_transition_bound_claim": True,
        "framed_head_call_and_counted_source_reflection_claim": True,
        "dependent_conditional_calls_with_physical_cost_claim": True,
        "physical_dense_word_decoding_claim": True,
        "encoded_input_physical_checked_body_claim": True,
        "physical_checked_worker_output_agreement_claim": True,
        "physical_checked_body_input_scope": "canonical dense fields of the admitted three-word task; outer candidate parsing and preparation remain separate",
        "physical_original_word_task_preparation_claim": True,
        "normalized_source_clock_prepared_body_join_claim": True,
        "admitted_prepared_worker_dyadic_source_bound_claim": True,
        "physical_candidate_pair_parser_claim": True,
        "entry_worker_dyadic_source_bound_claim": True,
        "required_entry_worker_theorems": list(ENTRY_WORKER_THEOREMS),
        "entry_worker_input_scope": "raw canonical pair-code candidate and original word in the actual two-field job; physical worker parsing/preparation/checking are charged",
        "full_initialization_observer_scheduler_cost_claim": False,
        "compiled_clean_halts_scope": "admitted trList inputs; no malformed-input clean-halt assumption",
        "source_in_place_output_needs_explicit_input_charge": True,
        "IC_specific_source_machine_wrapper_completed_claim": True,
        "hierarchical_profile": "retained inclusive-phase clocks; complete pair records; cumulative 8*2^L factors",
        "hierarchical_native_scheduler_correspondence_claim": False,
        "hierarchical_uncharged_kernel_costs": ["phase-list-construction", "worker-initialization",
                                              "output-decoding", "description-materialization"],
        "native_speedup_claim": False,
    }
    (output / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": True, "original_bridge_declarations":
                      original_audit["originalInterpreterBridgeDeclarations"],
                      "original_prefix_declarations": prefix_audit["originalPrefixBridgeDeclarations"],
                      "shared_interface_declarations": shared_audit["sharedInterpreterInterfaceDeclarations"],
                      "framed_prefix_declarations": framed_audit["framedPrefixDeclarations"],
                      "hierarchical_search_declarations": hierarchical_audit["hierarchicalSearchDeclarations"],
                      "source_machine_time_declarations": source_time_audit["sourceMachineTimeDeclarations"],
                      "original_reference_warning_count": len(reference_warnings),
                      "receipt": str(output / "receipt.json")}))


if __name__ == "__main__":
    main()
