#!/usr/bin/env python3
"""Executable source/bytecode checks for the three Nil rule-machine guests."""

from __future__ import annotations

from pathlib import Path
import argparse
import json
import subprocess
import sys
import tempfile

import gslt2parse_schema_v1 as sx


class GateFailure(RuntimeError):
    pass


def run_json(command: list[str]) -> dict[str, object]:
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if completed.returncode != 0:
        raise GateFailure(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure(f"non-JSON result: {completed.stdout!r}") from error


def query(
    chart: Path,
    core: Path,
    guest: Path,
    text: str,
    *,
    rounds: int = 2000,
    extra: tuple[Path, ...] = (),
) -> dict[str, object]:
    return run_json(
        [
            str(chart),
            str(core),
            *(str(path) for path in extra),
            str(guest),
            "--query-text",
            text,
            "--max-rounds",
            str(rounds),
            "--max-nodes",
            "15000000",
            "--timeout",
            "30",
        ]
    )


def expect_outcome(result: dict[str, object], outcome: str, answers: int) -> None:
    if result.get("outcome") != outcome or result.get("answers") != answers:
        raise GateFailure(
            f"expected {outcome}/{answers}, got "
            f"{result.get('outcome')}/{result.get('answers')}: {result}"
        )


def occurrence(result: dict[str, object]) -> str:
    terms = result.get("terms")
    if not isinstance(terms, list) or len(terms) != 1 or not isinstance(terms[0], str):
        raise GateFailure(f"expected one rendered answer term: {result}")
    parsed = sx.parse_sexprs(terms[0], source="chart answer")
    if len(parsed) != 1 or not isinstance(parsed[0], tuple):
        raise GateFailure(f"malformed chart answer term: {terms[0]}")
    answer = parsed[0]
    observed = answer[-1]
    if (
        not isinstance(observed, tuple)
        or len(observed) != 2
        or not isinstance(observed[0], sx.Symbol)
        or observed[0].text != "occurrence"
    ):
        raise GateFailure(f"answer does not end in an occurrence: {terms[0]}")
    return sx.render(observed)


def decode_list(term: sx.SExpr) -> list[sx.SExpr]:
    items: list[sx.SExpr] = []
    while isinstance(term, tuple):
        if (
            len(term) != 3
            or not isinstance(term[0], sx.Symbol)
            or term[0].text != "rm-cons"
        ):
            raise GateFailure(f"malformed proof argument list: {sx.render(term)}")
        items.append(term[1])
        term = term[2]
    if not isinstance(term, sx.Symbol) or term.text != "rm-nil":
        raise GateFailure(f"open proof argument list: {sx.render(term)}")
    return items


def curried_source_proof(result: dict[str, object]) -> str:
    rendered = occurrence(result)
    parsed = sx.parse_sexprs(rendered, source="curried proof occurrence")
    if len(parsed) != 1 or not isinstance(parsed[0], tuple):
        raise GateFailure(f"malformed curried proof occurrence: {rendered}")
    occurrence_term = parsed[0]
    if len(occurrence_term) != 2:
        raise GateFailure(f"malformed curried proof occurrence: {rendered}")

    def translate(term: sx.SExpr) -> sx.SExpr:
        if not isinstance(term, tuple) or not term or not isinstance(term[0], sx.Symbol):
            raise GateFailure(f"unsupported internal proof: {sx.render(term)}")
        if term[0].text == "rm-proof-atom" and len(term) == 2:
            return term[1]
        if term[0].text == "rm-proof-app" and len(term) == 3:
            value: sx.SExpr = term[1]
            for argument in decode_list(term[2]):
                value = (value, translate(argument))
            return value
        raise GateFailure(f"unsupported internal proof: {sx.render(term)}")

    return sx.render(translate(occurrence_term[1]))


def check_regeneration(
    root: Path, nil_root: Path, generated: Path, curried_generated: Path,
    runtime_generated: Path,
) -> None:
    generator = root / "tools" / "generate_nil_rule_guests_v1.py"
    with tempfile.TemporaryDirectory(prefix="nil-rule-guests-v1-") as raw:
        candidate = Path(raw) / generated.name
        curried_candidate = Path(raw) / curried_generated.name
        runtime_candidate = Path(raw) / runtime_generated.name
        command = [
            sys.executable,
            str(generator),
            "--curried",
            str(
                nil_root
                / "experimental/curried-chaining/curried-chainer.metta"
            ),
            "--bfc",
            str(nil_root / "experimental/backward-via-forward/bfc-xp.mm2"),
            "--synthesis",
            str(nil_root / "experimental/synthesis/SynthesizeTest.metta"),
            "--sumo-kb",
            str(
                nil_root
                / "experimental/sumo/john-carry-flower/john-carry-flower.kif.metta"
            ),
            "--sumo-rules",
            str(nil_root / "experimental/sumo/rule-base.metta"),
            "--out",
            str(candidate),
            "--curried-out",
            str(curried_candidate),
            "--runtime-out",
            str(runtime_candidate),
        ]
        completed = subprocess.run(command, text=True, capture_output=True, check=False)
        if completed.returncode != 0:
            raise GateFailure(
                f"guest regeneration failed:\n{completed.stdout}\n{completed.stderr}"
            )
        if candidate.read_bytes() != generated.read_bytes():
            raise GateFailure("identity-pinned SUMO guest is stale")
        if curried_candidate.read_bytes() != curried_generated.read_bytes():
            raise GateFailure("identity-pinned curried-chaining guest is stale")
        if runtime_candidate.read_bytes() != runtime_generated.read_bytes():
            raise GateFailure("identity-pinned native guest fixture is stale")


def check_rule_program_generation(
    root: Path, core: Path, rule_program: Path, generated: Path,
) -> None:
    generator = root / "tools" / "generate_rule_machine_program_v1.py"

    def run(rule_program_source: Path, output: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(generator),
                "--core",
                str(core),
                "--program-gslt",
                str(rule_program_source),
                "--out",
                str(output),
            ],
            text=True,
            capture_output=True,
            check=False,
        )

    with tempfile.TemporaryDirectory(prefix="rule-machine-program-v1-") as raw:
        directory = Path(raw)
        candidate = directory / generated.name
        completed = run(rule_program, candidate)
        if completed.returncode != 0:
            raise GateFailure(
                f"RuleMachineProgram GSLT generation failed:\n{completed.stdout}\n"
                f"{completed.stderr}"
            )
        if candidate.read_bytes() != generated.read_bytes():
            raise GateFailure("runtime-consumed RuleMachineProgram GSLT table is stale")

        source = rule_program.read_text(encoding="utf-8")
        mutation = "(rmbc-hyp-add 2)"
        if source.count(mutation) != 1:
            raise GateFailure("RuleMachineProgram semantic mutation anchor changed")
        mutated_rule_program = directory / rule_program.name
        mutated_rule_program.write_text(
            source.replace(mutation, "(rmbc-hyp-add 1)"), encoding="utf-8"
        )
        mutated = directory / "mutated.generated.h"
        completed = run(mutated_rule_program, mutated)
        if completed.returncode != 0:
            raise GateFailure(
                f"semantic mutant generation failed unexpectedly:\n"
                f"{completed.stdout}\n{completed.stderr}"
            )
        if mutated.read_bytes() == generated.read_bytes():
            raise GateFailure("RuleMachineProgram semantic mutation did not change runtime tables")
        def semantic_payload(path: Path) -> tuple[str, ...]:
            return tuple(
                line
                for line in path.read_text(encoding="utf-8").splitlines()
                if not line.startswith("#define RM_RULE_PROGRAM_GSLT_")
            )

        if semantic_payload(mutated) == semantic_payload(generated):
            raise GateFailure(
                "RuleMachineProgram mutation changed only identity, not executable tables"
            )

        missing_rule_rule_program = directory / "missing-rule.metta"
        missing_rule_rule_program.write_text(
            source.replace(
                "compile-rule-program-inverse-mp", "compile-rule-program-inverse-mp-removed", 1
            ),
            encoding="utf-8",
        )
        missing = directory / "missing.generated.h"
        completed = run(missing_rule_rule_program, missing)
        if completed.returncode == 0:
            raise GateFailure("missing RuleMachineProgram semantic rule did not fail generation")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--nil-root", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    presentations = args.root / "experiments/gslt2parse_foundation/presentations"
    core = presentations / "core/rule_machine_core_v1.metta"
    rule_program = presentations / "specializations/rule_machine_hilbert_bfc_program_v1.metta"
    bfc = presentations / "languages/nil_bfc_rule_package_v1.metta"
    curried = presentations / "languages/curried_chaining_rule_package_v1.metta"
    synthesis = presentations / "languages/nil_typed_synthesis_rule_package_v1.metta"
    sumo = presentations / "languages/nil_sumo_john_carry_flower_v1.metta"
    runtime_generated = args.root / "tests/prime/nil_rule_machine_guests.generated.metta"
    rule_program_generated = (
        args.root / "src/generated/rule_machine_program_v1.generated.h"
    )

    check_regeneration(
        args.root, args.nil_root, sumo, curried, runtime_generated
    )
    check_rule_program_generation(args.root, core, rule_program, rule_program_generated)
    for guest in (curried, bfc, synthesis, sumo):
        sx.admit([core, guest])
    sx.admit([core, rule_program, bfc])

    curried_b_source = query(
        args.chart,
        core,
        curried,
        "(guest-observe proof-occurrence-bag curried-chaining r0 "
        "(nat-s (nat-s nat-z)) B ?occurrence)",
    )
    curried_b_bytecode = query(
        args.chart,
        core,
        curried,
        "(guest-bc-observe proof-occurrence-bag curried-chaining r0 "
        "(nat-s (nat-s nat-z)) B ?occurrence)",
    )
    expect_outcome(curried_b_source, "Unique", 1)
    expect_outcome(curried_b_bytecode, "Unique", 1)
    if occurrence(curried_b_source) != occurrence(curried_b_bytecode):
        raise GateFailure("curried B source/bytecode proof differs")
    if curried_source_proof(curried_b_source) != "((ModusPonens ab) a)":
        raise GateFailure("curried B proof no longer reconstructs the authored proof")

    curried_c_source = query(
        args.chart,
        core,
        curried,
        "(guest-observe proof-occurrence-bag curried-chaining r0 "
        "(nat-s (nat-s (nat-s nat-z))) C ?occurrence)",
    )
    curried_c_bytecode = query(
        args.chart,
        core,
        curried,
        "(guest-bc-observe proof-occurrence-bag curried-chaining r0 "
        "(nat-s (nat-s (nat-s nat-z))) C ?occurrence)",
    )
    expect_outcome(curried_c_source, "Unique", 1)
    expect_outcome(curried_c_bytecode, "Unique", 1)
    if occurrence(curried_c_source) != occurrence(curried_c_bytecode):
        raise GateFailure("curried C source/bytecode proof differs")
    expected_curried_c = "((ModusPonens bc) ((ModusPonens ab) a))"
    if curried_source_proof(curried_c_source) != expected_curried_c:
        raise GateFailure("curried C proof no longer reconstructs the authored proof")

    curried_negative = query(
        args.chart,
        core,
        curried,
        "(guest-observe proof-occurrence-bag curried-chaining r0 "
        "(nat-s (nat-s (nat-s nat-z))) D ?occurrence)",
    )
    expect_outcome(curried_negative, "NoAnswer", 0)

    compiled_bfc = query(
        args.chart,
        core,
        bfc,
        "(compile-source-block nil-bfc ?source ?bytecode)",
    )
    expect_outcome(compiled_bfc, "Ambiguous", 4)

    rule_program_bfc = query(
        args.chart,
        core,
        bfc,
        "(compile-rule-program-source nil-bfc ?id ?program)",
        extra=(rule_program,),
    )
    expect_outcome(rule_program_bfc, "Ambiguous", 4)
    rule_program_terms = rule_program_bfc.get("terms")
    if not isinstance(rule_program_terms, list) or any(
        "rmbc-emit" not in str(term) for term in rule_program_terms
    ):
        raise GateFailure("RuleMachineProgram compiler did not emit instruction programs")

    rule_program_reject = query(
        args.chart,
        core,
        bfc,
        "(compile-rule-program-block "
        "(bc-block fake source (bc-match c) "
        "(bc-goals (rm-cons (rm-premise p a) rm-nil)) "
        "(bc-build (rm-proof-atom fake)) bc-emit) ?program)",
        extra=(rule_program,),
    )
    expect_outcome(rule_program_reject, "NoAnswer", 0)

    old_blackbird = query(
        args.chart,
        core,
        synthesis,
        "(guest-observe proof-occurrence-bag nil-typed-synthesis r0 "
        "(nat-s nat-z) (arrow String Number Number) ?occurrence)",
    )
    expect_outcome(old_blackbird, "NoAnswer", 0)

    new_blackbird = query(
        args.chart,
        core,
        synthesis,
        "(guest-observe proof-occurrence-bag nil-typed-synthesis r1 "
        "(nat-s nat-z) (arrow String Number Number) ?occurrence)",
    )
    compiled_blackbird = query(
        args.chart,
        core,
        synthesis,
        "(guest-bc-observe proof-occurrence-bag nil-typed-synthesis r1 "
        "(nat-s nat-z) (arrow String Number Number) ?occurrence)",
    )
    expect_outcome(new_blackbird, "Unique", 1)
    expect_outcome(compiled_blackbird, "Unique", 1)
    expected_blackbird = (
        "(occurrence (rm-proof-app blackbird "
        "(rm-cons (rm-proof-atom h) (rm-cons (rm-proof-atom i) rm-nil))))"
    )
    if occurrence(new_blackbird) != expected_blackbird:
        raise GateFailure("source Blackbird proof changed")
    if occurrence(compiled_blackbird) != expected_blackbird:
        raise GateFailure("bytecode Blackbird proof changed")

    source_compose = query(
        args.chart,
        core,
        synthesis,
        "(guest-observe proof-occurrence-bag nil-typed-synthesis r0 "
        "(nat-s nat-z) (arrow Number Bool) ?occurrence)",
    )
    bytecode_compose = query(
        args.chart,
        core,
        synthesis,
        "(guest-bc-observe proof-occurrence-bag nil-typed-synthesis r0 "
        "(nat-s nat-z) (arrow Number Bool) ?occurrence)",
    )
    expect_outcome(source_compose, "Unique", 1)
    expect_outcome(bytecode_compose, "Unique", 1)
    if occurrence(source_compose) != occurrence(bytecode_compose):
        raise GateFailure("source/bytecode composition evidence differs")

    one_block_link = query(
        args.chart,
        core,
        synthesis,
        "(compile-link (bc-artifact-empty nil-typed-synthesis) "
        "(block-ref nil-typed-synthesis blackbird) ?linked)",
    )
    expect_outcome(one_block_link, "Unique", 1)
    linked_terms = one_block_link.get("terms")
    if not isinstance(linked_terms, list) or len(linked_terms) != 1:
        raise GateFailure("one-block publication did not return one artifact")
    if str(linked_terms[0]).count("bc-block-ref") != 1:
        raise GateFailure("incremental publication constructed more than one block ref")

    sumo_negative = query(
        args.chart,
        core,
        sumo,
        "(guest-observe proof-occurrence-bag nil-sumo-john-carry-flower r0 "
        "(nat-s (nat-s (nat-s nat-z))) "
        "(objectTransferred JohnsCarry JohnsFlower) ?occurrence)",
        rounds=10000,
    )
    expect_outcome(sumo_negative, "NoAnswer", 0)

    sumo_source = query(
        args.chart,
        core,
        sumo,
        "(guest-observe proof-occurrence-bag nil-sumo-john-carry-flower r0 "
        "(nat-s (nat-s (nat-s (nat-s nat-z)))) "
        "(objectTransferred JohnsCarry JohnsFlower) ?occurrence)",
        rounds=20000,
    )
    sumo_bytecode = query(
        args.chart,
        core,
        sumo,
        "(guest-bc-observe proof-occurrence-bag nil-sumo-john-carry-flower r0 "
        "(nat-s (nat-s (nat-s (nat-s nat-z)))) "
        "(objectTransferred JohnsCarry JohnsFlower) ?occurrence)",
        rounds=30000,
    )
    expect_outcome(sumo_source, "Unique", 1)
    expect_outcome(sumo_bytecode, "Unique", 1)
    if occurrence(sumo_source) != occurrence(sumo_bytecode):
        raise GateFailure("source/bytecode SUMO proof differs")
    rendered_sumo = occurrence(sumo_source)
    for required in (
        "ModusPonens",
        "TrinaryConjunctionIntroduction",
        "BinaryConjunctionIntroduction",
        "JohnsFlower",
    ):
        if required not in rendered_sumo:
            raise GateFailure(f"SUMO proof lost {required}")

    print("(RuleMachineGSLTV1Summary 21 21 0)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, sx.SchemaError, OSError) as error:
        print(f"test_rule_machine_gslt_v1: {error}", file=sys.stderr)
        raise SystemExit(1)
