#!/usr/bin/env python3
"""Differential and adversarial gate for Prime's admitted reader presentation."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess
import sys


@dataclass(frozen=True)
class ReaderCase:
    label: str
    source: str
    outcome: str


CASES = (
    ReaderCase("empty-document", "", "Unique"),
    ReaderCase("comment-at-eof", " \t; no newline", "Unique"),
    ReaderCase("quote-symbol", "@doc", "Unique"),
    ReaderCase("quote-string", '@"moo"', "Unique"),
    ReaderCase("quote-expression", "@(agent Max Botnick)", "Unique"),
    ReaderCase("quote-open-syntax", "@($x)", "Unique"),
    ReaderCase("unquote-symbol", "*@doc", "Unique"),
    ReaderCase("unquote-expression", "*@(agent Max Botnick)", "Unique"),
    ReaderCase("ordinary-variable", "$x", "Unique"),
    ReaderCase("structural-variable-symbol", "$@foo", "Unique"),
    ReaderCase("structural-variable-string", '$@"moo"', "Unique"),
    ReaderCase("structural-variable-expression", '$@(mm-var "ph")', "Unique"),
    ReaderCase("ordinary-space", "&self", "Unique"),
    ReaderCase("structural-reference-symbol", "&@doc", "Unique"),
    ReaderCase("structural-reference-expression", "&@(agent Max Botnick)", "Unique"),
    ReaderCase("ordinary-star-head", "(* a b)", "Unique"),
    ReaderCase(
        "structural-lambda-name",
        '(lam @(mm-var "ph") (wff @(mm-var "ph")))',
        "Unique",
    ),
    ReaderCase("at-inside-symbol", "foo@bar", "Unique"),
    ReaderCase("at-inside-variable", "$foo@bar", "Unique"),
    ReaderCase("escaped-string", '"a\\\"b"', "Unique"),
    ReaderCase("empty-string", '""', "Unique"),
    ReaderCase("non-ascii-symbol", "λ牛", "Unique"),
    ReaderCase("non-ascii-name", '@"κόσμος"', "Unique"),
    ReaderCase("adjacent-atoms", "(a(b)c)", "Unique"),
    ReaderCase("canonical-numbers", "42 -3 1.5 3/4", "Unique"),
    ReaderCase("missing-close", "(a", "NoParse"),
    ReaderCase("stray-close", ")", "NoParse"),
    ReaderCase("unterminated-string", '"unterminated', "NoParse"),
    ReaderCase("quote-without-payload", "@", "NoParse"),
    ReaderCase("structural-variable-without-payload", "$@", "NoParse"),
    ReaderCase("structural-reference-without-payload", "&@", "NoParse"),
    ReaderCase("open-structural-variable", "$@(open $x)", "NoParse"),
    ReaderCase("open-structural-reference", "&@(open $x)", "NoParse"),
    ReaderCase("unquote-missing-quote-payload", "*@", "NoParse"),
)

MUTATION_CASES = (
    "@doc",
    '$@"moo"',
    "&@(agent Max Botnick)",
    "*@doc",
)

MUTATION_CONTROLS = (
    "(a b)",
    "foo@bar",
    "$foo@bar",
    "(* a b)",
    "&self",
)


class GateFailure(RuntimeError):
    pass


def invoke_json(argv: list[str], *, expect_rc: int = 0) -> dict[str, object]:
    proc = subprocess.run(argv, text=True, capture_output=True, check=False)
    if proc.returncode != expect_rc:
        raise GateFailure(
            f"command exited {proc.returncode}, expected {expect_rc}: "
            f"{' '.join(argv[:2])}\nstdout: {proc.stdout}\nstderr: {proc.stderr}"
        )
    lines = [line for line in proc.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise GateFailure(f"expected one JSON line, received {len(lines)}: {proc.stdout!r}")
    try:
        value = json.loads(lines[0])
    except json.JSONDecodeError as exc:
        raise GateFailure(f"invalid JSON output: {proc.stdout!r}") from exc
    if not isinstance(value, dict):
        raise GateFailure(f"JSON result is not an object: {value!r}")
    return value


def engine_command(
    engine: Path,
    core: Path,
    presentation: Path,
    source: str,
    *extra: str,
) -> list[str]:
    return [
        str(engine),
        str(core),
        str(presentation),
        "--root",
        "metta-document",
        "--input-text",
        source,
        *extra,
    ]


def require_outcome(result: dict[str, object], expected: str, context: str) -> None:
    actual = result.get("outcome")
    if actual != expected:
        raise GateFailure(f"{context}: expected {expected}, received {actual}: {result}")
    if actual == "Ambiguous":
        raise GateFailure(f"{context}: Prime reader ambiguity is a finding: {result}")


def compare_case(
    case: ReaderCase,
    engine: Path,
    core: Path,
    presentation: Path,
    oracle: Path,
) -> None:
    native = invoke_json([str(oracle), "--input-text", case.source])
    packed = invoke_json(engine_command(engine, core, presentation, case.source))
    interpreted = invoke_json(
        engine_command(engine, core, presentation, case.source, "--interpreted")
    )
    require_outcome(native, case.outcome, f"{case.label}/native")
    require_outcome(packed, case.outcome, f"{case.label}/packed")
    require_outcome(interpreted, case.outcome, f"{case.label}/interpreted")
    native_asts = native.get("asts", [])
    packed_asts = packed.get("asts", [])
    interpreted_asts = interpreted.get("asts", [])
    if native_asts != packed_asts or packed_asts != interpreted_asts:
        raise GateFailure(
            f"{case.label}: complete AST sets disagree\n"
            f"native={native_asts}\npacked={packed_asts}\n"
            f"interpreted={interpreted_asts}"
        )


def verify_rule_deletion(
    engine: Path,
    core: Path,
    presentation: Path,
    mutant: Path,
    oracle: Path,
) -> int:
    required_ids = (
        "parse-prime-quote",
        "parse-prime-unquote",
        "parse-prime-structural-variable",
        "parse-prime-resolve-name",
    )
    baseline_text = presentation.read_text(encoding="utf-8")
    mutant_text = mutant.read_text(encoding="utf-8")
    for rule_id in required_ids:
        if baseline_text.count(rule_id) != 1:
            raise GateFailure(f"baseline rule inventory drifted for {rule_id}")
        if rule_id in mutant_text:
            raise GateFailure(f"universal-name rule survived deletion: {rule_id}")

    killed = 0
    for source in MUTATION_CASES:
        native = invoke_json([str(oracle), "--input-text", source])
        require_outcome(native, "Unique", f"deletion-native/{source}")
        for mode in ((), ("--interpreted",)):
            result = invoke_json(engine_command(engine, core, mutant, source, *mode))
            require_outcome(result, "NoParse", f"deletion-mutant/{source}/{mode}")
        killed += 1

    for source in MUTATION_CONTROLS:
        native = invoke_json([str(oracle), "--input-text", source])
        mutant_result = invoke_json(engine_command(engine, core, mutant, source))
        require_outcome(native, "Unique", f"deletion-control-native/{source}")
        require_outcome(mutant_result, "Unique", f"deletion-control-mutant/{source}")
        if native.get("asts") != mutant_result.get("asts"):
            raise GateFailure(f"deletion changed an ordinary-language control: {source}")
    return killed


def replace_root_certificate_fields(cert: str) -> dict[str, str]:
    match = re.match(r"^\(cert ([^ ]+) (-?\d+) (-?\d+) \(", cert)
    if not match:
        raise GateFailure("certificate root does not have the admitted four-field form")
    rule_id, start_text, end_text = match.groups()
    start = int(start_text)
    end = int(end_text)
    suffix = cert[match.end() :]
    return {
        "rule": f"(cert rule-not-in-presentation {start} {end} ({suffix}",
        "start": f"(cert {rule_id} {start + 1} {end} ({suffix}",
        "end": f"(cert {rule_id} {start} {end + 1} ({suffix}",
        "children": f"(cert {rule_id} {start} {end} ())",
    }


def verify_certificate_replay(
    engine: Path,
    core: Path,
    presentation: Path,
    artifact_dir: Path,
) -> int:
    source = "&@(agent Max Botnick)"
    result = invoke_json(
        engine_command(engine, core, presentation, source, "--certs")
    )
    require_outcome(result, "Unique", "certificate-producer")
    certs = result.get("certs")
    if not isinstance(certs, list) or len(certs) != 1 or not isinstance(certs[0], str):
        raise GateFailure(f"expected one replayable certificate: {certs!r}")

    artifact_dir.mkdir(parents=True, exist_ok=True)
    valid_path = artifact_dir / "prime_syntax_valid.cert"
    valid_path.write_text(certs[0] + "\n", encoding="utf-8")
    accepted = invoke_json(
        engine_command(
            engine, core, presentation, source, "--replay", str(valid_path)
        )
    )
    if accepted.get("replay") != "ACCEPT":
        raise GateFailure(f"honest certificate was rejected: {accepted}")

    rejected = 0
    for field, mutated in replace_root_certificate_fields(certs[0]).items():
        mutation_path = artifact_dir / f"prime_syntax_mutated_{field}.cert"
        mutation_path.write_text(mutated + "\n", encoding="utf-8")
        verdict = invoke_json(
            engine_command(
                engine,
                core,
                presentation,
                source,
                "--replay",
                str(mutation_path),
            ),
            expect_rc=1,
        )
        if verdict.get("replay") != "REJECT":
            raise GateFailure(f"{field} certificate mutation survived: {verdict}")
        rejected += 1
    return rejected


def verify_engine_purity(engine_source: Path) -> int:
    text = engine_source.read_text(encoding="utf-8")
    guest_names = re.compile(
        r"\b(?:Prime|MeTTa|Metamath|TPTP|Megalodon|Rholang|PeTTa|CeTTa)\b",
        re.IGNORECASE,
    )
    hits = guest_names.findall(text)
    if hits:
        raise GateFailure(f"guest-language policy leaked into generic engine: {hits}")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--engine-source", type=Path, required=True)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--presentation", type=Path, required=True)
    parser.add_argument("--mutant", type=Path, required=True)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    args = parser.parse_args()

    try:
        for case in CASES:
            compare_case(case, args.engine, args.core, args.presentation, args.oracle)
        deleted = verify_rule_deletion(
            args.engine, args.core, args.presentation, args.mutant, args.oracle
        )
        certificate_mutations = verify_certificate_replay(
            args.engine, args.core, args.presentation, args.artifact_dir
        )
        purity_files = verify_engine_purity(args.engine_source)
    except (GateFailure, OSError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    print(
        "(PrimeSyntaxGSLTSummary "
        f"{len(CASES)} {len(CASES)} 0 "
        f"{deleted} {deleted} 0 "
        f"{certificate_mutations} {certificate_mutations} 0 "
        f"{purity_files} {purity_files} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
