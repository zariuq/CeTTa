#!/usr/bin/env python3
"""Native C execution gates for the finite-Horn GSLT compiler path."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"

COMMON = (
    "parserpack/parser_pack_core_v1.metta",
    "reflection/finite_horn_reflection_v1.metta",
    "compiler/syntax_compiler_v1.metta",
    "compiler/relational_cfg_lowering_v1.metta",
    "compiler/relational_cfg_link_v1.metta",
    "compiler/parser_pack_compiler_v1.metta",
    "compiler/parser_action_bytecode_compiler_v1.metta",
)

CASES = {
    "prime": {
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/ground_relations_v1.metta",
            "shared/cetta_prime_scalar_classes_v1.metta",
            "languages/cetta_prime_reader_v1.metta",
        ),
        "productions": (
            179,
            "b3d25912cf037b8785f45979d62951c69814d0556d0c53e0788bc504211fe669",
        ),
        "classes": (
            14,
            "9158ab8e1e5d7839bc1c8cdb54ecd1275fb3affc28b1a45077002e490e9e39d6",
        ),
        "actions": (
            179,
            "b9194af90380048ffbf7a50f0b54f560fe7c5dac97116c961a17bbfa56a4f833",
        ),
    },
    "metamath": {
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "languages/metamath_appendix_e_v1.metta",
        ),
        "productions": (
            270,
            "dc6d68b5a0996a3201bd87f7586c6ffa0b2585c240551fc0003a64e34df9d41f",
        ),
        "classes": (
            375,
            "eb5cfda104e75f5fc5d623fc7ca27a835814963274201784c80a951fca628989",
        ),
        "actions": (
            270,
            "738417b94ad546c87d59dab83985475a8abff6f4e441a4f93148974576150e39",
        ),
    },
    "megalodon": {
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/char_core_v1.metta",
            "languages/megalodon_dynamic_v1.metta",
        ),
        "productions": (
            138,
            "065d6c453713b2d0610cfb37e2530ba0d661a880985e3c7268dc09eae63c0b68",
        ),
        "classes": (
            563,
            "ecbfa56c1067577bdde5a687764aae6b0b1b640316e0b4bafd8f1abd9b3895bb",
        ),
        "actions": (
            138,
            "954beee010b5e577b86a642fafb1fd520e2d0d03b9ba75587b948d755ffd51b8",
        ),
    },
    "tptp": {
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/char_core_v1.metta",
            "languages/tptp_fof_cnf_v1.metta",
        ),
        "productions": (
            688,
            "0117175e8ddf25f5a2563a418572fd28cb83cf9e8b9fe761c3dd231663822373",
        ),
        "classes": (
            562,
            "0c778826915ed484c5d1d3f3df92fca4c12d16a2e326a44ffcd6cb244487a90a",
        ),
        "actions": (
            688,
            "d8882993f494f523f6214bffef652e772e3663dac518cd0e917748dc06e4fbbd",
        ),
    },
}


class GateFailure(RuntimeError):
    pass


def run_json(binary: Path, arguments: list[str], expected_code: int = 0) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        timeout=60,
    )
    if completed.returncode != expected_code:
        raise GateFailure(
            f"unexpected exit {completed.returncode}: {completed.stderr.strip()}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure(
            f"native engine emitted non-JSON output: {completed.stdout!r}"
        ) from error


def program_arguments(case: dict) -> list[str]:
    arguments = [str(PRESENTATIONS / relative) for relative in COMMON]
    for relative in case["sources"]:
        arguments.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    return arguments


def check_compiler_matrix(binary: Path) -> int:
    passed = 0
    queries = {
        "productions": "(compile-pack-production ?owner ?production)",
        "classes": "(compile-pack-class-clause ?owner ?clause)",
        "actions": (
            "(compile-pack-action-program "
            "?owner ?label ?arity ?action ?code)"
        ),
    }
    for label, case in CASES.items():
        base = program_arguments(case)
        for result_kind, query in queries.items():
            expected_count, expected_digest = case[result_kind]
            result = run_json(
                binary,
                [*base, "--query-text", query, "--summary", "--timeout", "30"],
            )
            if result.get("answers") != expected_count:
                raise GateFailure(
                    f"{label} {result_kind}: expected {expected_count} answers, "
                    f"got {result.get('answers')}"
                )
            if result.get("term_digest") != expected_digest:
                raise GateFailure(
                    f"{label} {result_kind}: answer-set digest changed"
                )
            if result.get("outcome") not in {"Unique", "Ambiguous"}:
                raise GateFailure(
                    f"{label} {result_kind}: incomplete outcome {result.get('outcome')}"
                )
            passed += 1
    return passed


def check_action_bytecode_fragment(binary: Path) -> int:
    base = program_arguments(CASES["prime"])
    action = (
        "(pa-apply pair "
        "(pa-cons (pa-slot q-zero) "
        "(pa-cons (pa-const nil) pa-nil)))"
    )
    expected = (
        "(lower-pack-action "
        f"{action} "
        "(pbc-cons (pbc-push-slot q-zero) "
        "(pbc-cons (pbc-push-const nil) "
        "(pbc-cons (pbc-apply pair (q-succ (q-succ q-zero))) "
        "pbc-nil))))"
    )
    positive = run_json(
        binary,
        [*base, "--query-text", f"(lower-pack-action {action} ?code)"],
    )
    if positive.get("outcome") != "Unique" or positive.get("terms") != [
        expected
    ]:
        raise GateFailure("nested parser action did not lower uniquely")

    malformed_list = run_json(
        binary,
        [
            *base,
            "--query-text",
            "(lower-pack-action "
            "(pa-apply pair (pa-cons (pa-slot q-zero) malformed)) ?code)",
        ],
    )
    if malformed_list.get("outcome") != "NoAnswer":
        raise GateFailure("malformed parser-action list was compiled")

    unknown_action = run_json(
        binary,
        [*base, "--query-text", "(lower-pack-action (pa-unknown x) ?code)"],
    )
    if unknown_action.get("outcome") != "NoAnswer":
        raise GateFailure("unknown parser action was compiled")
    return 3


def check_declared_disequality(binary: Path) -> int:
    core = str(PRESENTATIONS / "core/syntax_core_v1.metta")
    declaration = str(PRESENTATIONS / "shared/ground_relations_v1.metta")
    positive = run_json(
        binary,
        [core, declaration, "--query-text", "(different (cp 97) (cp 98))", "--certs"],
    )
    if positive.get("outcome") != "Unique" or positive.get("answers") != 1:
        raise GateFailure("declared ground disequality did not succeed")
    certificates = positive.get("certs")
    if not isinstance(certificates, list) or len(certificates) != 1:
        raise GateFailure("ground disequality did not emit one certificate")

    equal = run_json(
        binary,
        [core, declaration, "--query-text", "(different (cp 97) (cp 97))", "--summary"],
    )
    if equal.get("outcome") != "NoAnswer":
        raise GateFailure("equal terms satisfied ground disequality")

    undeclared = run_json(
        binary,
        [core, "--query-text", "(different (cp 97) (cp 98))", "--summary"],
    )
    if undeclared.get("outcome") != "NoAnswer":
        raise GateFailure("undeclared intrinsic became available")
    return 3


def check_reflection_and_certificates(binary: Path) -> int:
    case = CASES["megalodon"]
    query = "(compile-pack-production mg-parse-name-list ?production)"
    base = program_arguments(case)
    positive = run_json(binary, [*base, "--query-text", query, "--summary"])
    if positive.get("outcome") != "Unique" or positive.get("answers") != 1:
        raise GateFailure("reflected Megalodon CFG clause did not lower uniquely")

    without_reflection = [str(PRESENTATIONS / relative) for relative in COMMON]
    without_reflection.extend(
        str(PRESENTATIONS / relative) for relative in case["sources"]
    )
    negative = run_json(
        binary,
        [*without_reflection, "--query-text", query, "--summary"],
    )
    if negative.get("outcome") != "NoAnswer":
        raise GateFailure("compiler found a reflected source rule without reflection")

    with tempfile.TemporaryDirectory(prefix="gslt2parse-c-horn-") as raw_temp:
        temp = Path(raw_temp)
        certificate = temp / "compiler.cert"
        emitted = run_json(
            binary,
            [*base, "--query-text", query, "--cert-out", str(certificate)],
        )
        if emitted.get("outcome") != "Unique" or not certificate.is_file():
            raise GateFailure("unique reflected compilation did not write a certificate")
        replay = run_json(
            binary,
            [*base, "--query-text", query, "--replay", str(certificate)],
        )
        if replay.get("replay") != "ACCEPT":
            raise GateFailure("compiler certificate did not replay")
        corrupt = temp / "compiler-corrupt.cert"
        text = certificate.read_text(encoding="utf-8")
        mutated = re.sub(
            r"\(cert compile-pack-relational-production\b",
            "(cert no-such-rule",
            text,
            count=1,
        )
        if mutated == text:
            raise GateFailure("certificate mutation did not change its rule")
        corrupt.write_text(mutated, encoding="utf-8")
        rejected = run_json(
            binary,
            [*base, "--query-text", query, "--replay", str(corrupt)],
            expected_code=1,
        )
        if rejected.get("replay") != "REJECT":
            raise GateFailure("corrupt compiler certificate was accepted")
    return 4


def check_schema_and_unicode(binary: Path) -> int:
    core = str(PRESENTATIONS / "core/syntax_core_v1.metta")
    wrong_arity = str(PRESENTATIONS / "mutations/two_char_wrong_arity_v1.metta")
    malformed = run_json(
        binary,
        [core, wrong_arity, "--query-text", "(compile-root x ?result)"],
    )
    if malformed.get("outcome") != "MalformedPresentation":
        raise GateFailure("wrong-arity presentation escaped strict admission")

    with tempfile.TemporaryDirectory(prefix="gslt2parse-c-unicode-") as raw_temp:
        temp = Path(raw_temp)
        presentation = temp / "unicode_v1.metta"
        presentation.write_text(
            "(gslt-presentation-v1 UnicodeV1 "
            "(signature) (equations) "
            "(rewrites (rule define-unicode-root "
            "(head (definition unicode-root "
            "(node unicode-token (char (cp 955))))) (body))))\n",
            encoding="utf-8",
        )
        valid = run_json(
            binary,
            [core, str(presentation), "--root", "unicode-root", "--input-text", "λ"],
        )
        if valid.get("outcome") != "Unique" or valid.get("asts") != [
            "(node unicode-token (cp 955))"
        ]:
            raise GateFailure("valid Unicode scalar did not parse natively")

        invalid_input = temp / "invalid-utf8.bin"
        invalid_input.write_bytes(b"\xc0\xaf")
        invalid = run_json(
            binary,
            [
                core,
                str(presentation),
                "--root",
                "unicode-root",
                "--input-file",
                str(invalid_input),
            ],
        )
        if invalid.get("outcome") != "MalformedInput":
            raise GateFailure("invalid UTF-8 was not rejected")
    return 3


def check_ground_term_carrier(binary: Path) -> int:
    passed = 0
    with tempfile.TemporaryDirectory(prefix="gslt2parse-c-ground-terms-") as raw_temp:
        temp = Path(raw_temp)
        presentation = (
            PRESENTATIONS
            / "canaries"
            / "finite_horn_ground_terms_v1.metta"
        )
        base = [str(presentation)]
        control = r'(string-fact "λ\n\b\f\t\r\"\\\u001f")'
        escaped_control = r'(string-fact "\u03bb\n\b\f\t\r\"\\\u001f")'
        for query in (control, escaped_control):
            result = run_json(binary, [*base, "--query-text", query])
            if result.get("outcome") != "Unique" or result.get("terms") != [
                control
            ]:
                raise GateFailure("raw and escaped Unicode strings diverged")
            passed += 1

        exact_queries = {
            r'(string-fact "\u0000")': r'(string-fact "\u0000")',
            r'(string-fact "same")': r'(string-fact "same")',
            "(string-fact same)": "(string-fact same)",
            r'(string-copy "same")': r'(string-copy "same")',
        }
        for query, expected in exact_queries.items():
            result = run_json(binary, [*base, "--query-text", query])
            if result.get("outcome") != "Unique" or result.get("terms") != [
                expected
            ]:
                raise GateFailure(f"ground term did not round-trip: {query}")
            passed += 1

        all_strings = run_json(
            binary, [*base, "--query-text", "(string-fact ?value)"]
        )
        expected_strings = {
            r'(string-fact "same")',
            "(string-fact same)",
            control,
            r'(string-fact "\u0000")',
        }
        if all_strings.get("outcome") != "Ambiguous" or set(
            all_strings.get("terms", [])
        ) != expected_strings:
            raise GateFailure("string/symbol identities were merged or lost")
        passed += 1

        integer_queries = {
            "(integer-fact 000123456789012345678901234567890)":
                "(integer-fact 123456789012345678901234567890)",
            "(integer-fact -000123456789012345678901234567890)":
                "(integer-fact -123456789012345678901234567890)",
        }
        for query, expected in integer_queries.items():
            result = run_json(binary, [*base, "--query-text", query])
            if result.get("outcome") != "Unique" or result.get("terms") != [
                expected
            ]:
                raise GateFailure("arbitrary integer identity was truncated")
            passed += 1

        integer_string = run_json(
            binary,
            [
                *base,
                "--query-text",
                r'(integer-fact "123456789012345678901234567890")',
            ],
        )
        if integer_string.get("outcome") != "NoAnswer":
            raise GateFailure("integer and same-spelling string were merged")
        passed += 1

        malformed_queries = (
            r'(string-fact "\x")',
            r'(string-fact "\ud800")',
            r'(string-fact "\udc00")',
            '(string-fact "line\nbreak")',
            '(string-fact "unterminated)',
            r'(string-fact "same"',
        )
        for query in malformed_queries:
            result = run_json(binary, [*base, "--query-text", query])
            if result.get("outcome") != "MalformedQuery":
                raise GateFailure(f"malformed query escaped reader: {query!r}")
            passed += 1

        invalid_query = temp / "invalid-query.bin"
        invalid_query.write_bytes(b'(string-fact "\xc0\x80")')
        result = run_json(
            binary,
            [*base, "--query-file", str(invalid_query)],
        )
        if result.get("outcome") != "MalformedQuery":
            raise GateFailure("invalid UTF-8 query escaped the term reader")
        passed += 1
    return passed


def check_generic_native_sources() -> int:
    sources = (
        ROOT
        / "experiments"
        / "gslt2parse_foundation"
        / "native"
        / "finite_horn_chart_v1.c",
        ROOT
        / "experiments"
        / "gslt2parse_foundation"
        / "native"
        / "finite_horn_gslt_v1.c",
    )
    forbidden = re.compile(r"metamath|megalodon|tptp", re.IGNORECASE)
    for source in sources:
        if forbidden.search(source.read_text(encoding="utf-8")):
            raise GateFailure(f"guest-language name leaked into {source.name}")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        raise GateFailure(f"native binary is absent: {binary}")

    matrix = check_compiler_matrix(binary)
    action_bytecode = check_action_bytecode_fragment(binary)
    disequality = check_declared_disequality(binary)
    reflection = check_reflection_and_certificates(binary)
    boundaries = check_schema_and_unicode(binary)
    ground_terms = check_ground_term_carrier(binary)
    purity = check_generic_native_sources()
    total = (
        matrix
        + action_bytecode
        + disequality
        + reflection
        + boundaries
        + ground_terms
        + purity
    )
    print(
        "(FiniteHornChartV1NativeSummary "
        f"{total} {matrix} {action_bytecode} {disequality} "
        f"{reflection} {boundaries} "
        f"{ground_terms} {purity} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
