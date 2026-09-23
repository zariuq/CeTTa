#!/usr/bin/env python3
"""Exact Megalodon definition conversion to Lean/NIK/C differential."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

import gslt2parse_schema_v1 as sx
import test_nik_megalodon_polymorphic_v1 as poly
from megalodon_definition_conversion_v1 import (
    TermDecl,
    declarations_valid_article,
    encode_declarations,
    first_difference,
    path_refl,
    path_step,
    poly_type_article,
    project_signature,
    reduce_imp_codomain,
    reduce_imp_domain,
)


def compile_declaration_validity(
    output: str,
) -> tuple[sx.SExpr, sx.SExpr, sx.SExpr, sx.SExpr]:
    parameter_name, definition_name, definition_type, _, _ = checked_source(output)
    proposition: poly.Tp = ("prop",)
    identity_body: poly.Tm = ("lam", proposition, ("var", 0))
    declarations: list[TermDecl] = [
        (definition_name, definition_type, identity_body),
        (parameter_name, proposition, None),
    ]
    signature = [(name, value_type) for name, value_type, _ in declarations]
    goal = poly.mono.app(
        "MTermDeclarationsValid", encode_declarations(declarations),
        poly.encode_signature(signature),
    )
    article = declarations_valid_article(declarations)

    bad_body: poly.Tm = ("named", parameter_name)
    bad_declarations: list[TermDecl] = [
        (definition_name, definition_type, bad_body),
        declarations[1],
    ]
    bad_goal = poly.mono.app(
        "MTermDeclarationsValid", encode_declarations(bad_declarations),
        poly.encode_signature(signature),
    )
    forged_article = poly.mono.proof_node(
        "megalodon-def-declarations-valid-definition",
        [poly.mono.app(definition_name), poly.encode_tp(definition_type),
         poly.encode_tm(bad_body), encode_declarations(declarations[1:]),
         poly.encode_signature(signature[1:])],
        [poly_type_article(0, definition_type),
         declarations_valid_article(declarations[1:]),
         poly.type_proof(signature[1:], 0, [], identity_body)[1]],
    )
    return goal, article, bad_goal, forged_article



def checked_source(output: str) -> tuple[str, str, poly.Tm, poly.Tm, sx.SExpr]:
    forms = sx.parse_sexprs(output, source="Megalodon -sexprinfo")
    parameters = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("PARAM")
    ]
    definitions = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("DEF")
    ]
    theorems = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("THM")
    ]
    deltas = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("DELTA")
    ]
    proofs = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("PROOF")
    ]
    if (
        len(parameters) != 1 or len(parameters[0]) != 6
        or not isinstance(parameters[0][2], sx.StringLiteral)
        or parameters[0][4] != (sx.Symbol("PROP"),)
        or len(definitions) != 1 or len(definitions[0]) != 7
        or not isinstance(definitions[0][2], sx.StringLiteral)
        or len(theorems) != 1 or len(theorems[0]) != 7
        or theorems[0][1] != sx.StringLiteral("definition_identity")
        or len(deltas) != 1 or len(deltas[0]) != 2
        or len(proofs) != 1 or len(proofs[0]) != 3
        or proofs[0][1] != theorems[0][1]
    ):
        raise SystemExit("unexpected checked Megalodon definition document")
    parameter_name = parameters[0][2].text
    definition_name = definitions[0][2].text
    definition_type = poly.parse_tp(definitions[0][4])
    definition_body = poly.parse_tm(definitions[0][5])
    theorem = poly.parse_tm(theorems[0][5])
    if deltas[0][1] != sx.StringLiteral(definition_name):
        raise SystemExit("Megalodon did not retain the used definition dependency")
    proposition = ("prop",)
    parameter = ("named", parameter_name)
    identity_body = ("lam", proposition, ("var", 0))
    identity_type = ("arr", proposition, proposition)
    domain = ("app", ("named", definition_name), parameter)
    if (
        definition_type != identity_type
        or definition_body != identity_body
        or theorem != ("imp", domain, parameter)
    ):
        raise SystemExit("checked definition canary changed semantic shape")
    return parameter_name, definition_name, definition_type, theorem, proofs[0][2]


def compile_document(output: str) -> tuple[sx.SExpr, sx.SExpr, sx.SExpr]:
    parameter_name, definition_name, definition_type, theorem, proof = (
        checked_source(output)
    )
    proposition: poly.Tp = ("prop",)
    parameter: poly.Tm = ("named", parameter_name)
    identity_named: poly.Tm = ("named", definition_name)
    identity_body: poly.Tm = ("lam", proposition, ("var", 0))
    definition_domain: poly.Tm = ("app", identity_named, parameter)
    reduced_domain: poly.Tm = ("app", identity_body, parameter)
    synthesized: poly.Tm = ("imp", definition_domain, definition_domain)
    normalized: poly.Tm = ("imp", parameter, parameter)

    declarations: list[TermDecl] = [
        (definition_name, definition_type, identity_body),
        (parameter_name, proposition, None),
    ]
    signature = [(name, value_type) for name, value_type, _ in declarations]
    encoded_declarations = encode_declarations(declarations)

    inferred, base_article = poly.compile_proof(proof, signature, 0, [], [])
    if inferred != synthesized:
        raise SystemExit("checked Megalodon proof changed its synthesized type")
    environment_article = poly.mono.proof_node(
        "megalodon-env-proof-base",
        [poly.mono.app("MPrimNil"), poly.encode_signature(signature),
         poly.encode_known([]), poly.mono.nat(0), poly.encode_type_context([]),
         poly.encode_proof_context([]), poly.encode_tm(synthesized)],
        [base_article],
    )

    definition_here = poly.mono.proof_node(
        "megalodon-def-member-here",
        [poly.mono.app(definition_name), poly.encode_tp(definition_type),
         poly.encode_tm(identity_body), encode_declarations(declarations[1:])],
        [],
    )
    delta = poly.mono.proof_node(
        "megalodon-def-reduce-delta",
        [encoded_declarations, poly.mono.app(definition_name),
         poly.encode_tp(definition_type), poly.encode_tm(identity_body)],
        [definition_here],
    )
    domain_delta = poly.mono.proof_node(
        "megalodon-def-reduce-app-function",
        [encoded_declarations, poly.encode_tm(identity_named),
         poly.encode_tm(identity_body), poly.encode_tm(parameter)],
        [delta],
    )
    beta_result, substitution = poly.substitute(0, parameter, ("var", 0))
    if beta_result != parameter:
        raise SystemExit("definition beta substitution changed result")
    domain_beta = poly.mono.proof_node(
        "megalodon-def-reduce-beta",
        [encoded_declarations, poly.encode_tp(proposition),
         poly.encode_tm(("var", 0)), poly.encode_tm(parameter),
         poly.encode_tm(parameter)],
        [substitution],
    )

    after_left_delta: poly.Tm = ("imp", reduced_domain, definition_domain)
    after_left_beta: poly.Tm = ("imp", parameter, definition_domain)
    after_right_delta: poly.Tm = ("imp", parameter, reduced_domain)
    synthesized_path = path_step(
        encoded_declarations, synthesized, after_left_delta, normalized,
        reduce_imp_domain(encoded_declarations, definition_domain,
                          reduced_domain, definition_domain, domain_delta),
        path_step(
            encoded_declarations, after_left_delta, after_left_beta, normalized,
            reduce_imp_domain(encoded_declarations, reduced_domain, parameter,
                              definition_domain, domain_beta),
            path_step(
                encoded_declarations, after_left_beta, after_right_delta,
                normalized,
                reduce_imp_codomain(encoded_declarations, parameter,
                                    definition_domain, reduced_domain,
                                    domain_delta),
                path_step(
                    encoded_declarations, after_right_delta, normalized,
                    normalized,
                    reduce_imp_codomain(encoded_declarations, parameter,
                                        reduced_domain, parameter, domain_beta),
                    path_refl(encoded_declarations, normalized),
                ),
            ),
        ),
    )
    declared_after_delta: poly.Tm = ("imp", reduced_domain, parameter)
    declared_path = path_step(
        encoded_declarations, theorem, declared_after_delta, normalized,
        reduce_imp_domain(encoded_declarations, definition_domain,
                          reduced_domain, parameter, domain_delta),
        path_step(
            encoded_declarations, declared_after_delta, normalized, normalized,
            reduce_imp_domain(encoded_declarations, reduced_domain, parameter,
                              parameter, domain_beta),
            path_refl(encoded_declarations, normalized),
        ),
    )
    conversion = poly.mono.proof_node(
        "megalodon-def-conversion-common",
        [encoded_declarations, poly.encode_tm(synthesized),
         poly.encode_tm(theorem), poly.encode_tm(normalized)],
        [synthesized_path, declared_path],
    )

    full_environment = poly.mono.app(
        "MFullEnvironment", poly.mono.app("MPrimNil"),
        encoded_declarations, poly.encode_known([]),
    )
    goal = poly.mono.app(
        "MDefinitionProves", full_environment, poly.mono.nat(0),
        poly.encode_type_context([]), poly.encode_proof_context([]),
        poly.encode_tm(theorem),
    )
    article = poly.mono.proof_node(
        "megalodon-def-proof",
        [poly.mono.app("MPrimNil"), encoded_declarations,
         poly.encode_signature(signature), poly.encode_known([]),
         poly.mono.nat(0), poly.encode_type_context([]),
         poly.encode_proof_context([]), poly.encode_tm(synthesized),
         poly.encode_tm(theorem)],
        [project_signature(declarations), environment_article, conversion],
    )
    wrong_goal = poly.mono.app(
        "MDefinitionProves", full_environment, poly.mono.nat(0),
        poly.encode_type_context([]), poly.encode_proof_context([]),
        poly.encode_tm(synthesized),
    )
    return goal, article, wrong_goal


def read_witness(path: Path) -> tuple[sx.SExpr, sx.SExpr]:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if (
        len(forms) != 1 or not isinstance(forms[0], tuple)
        or len(forms[0]) != 3
        or forms[0][0] != sx.Symbol("nik-megalodon-definition-witness-v1")
    ):
        raise SystemExit("invalid Lean Megalodon definition witness")
    return forms[0][1], forms[0][2]


def check_catalog(
    path: Path, _goal: sx.SExpr, _article: sx.SExpr,
) -> None:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1 or not isinstance(forms[0], tuple):
        raise SystemExit("invalid NIK authority catalog")
    for authority in forms[0][1:]:
        if (
            isinstance(authority, tuple) and len(authority) == 7
            and authority[0] == sx.Symbol("authority")
            and authority[1] == sx.Symbol("MEGALODON-TERM")
        ):
            if (
                authority[2] != sx.StringLiteral(
                    "megalodon.mathdata.definition-conversion"
                )
                or authority[3] != sx.StringLiteral("11")
            ):
                raise SystemExit("Megalodon definition authority identity changed")
            rendered = sx.render(authority[5])
            for required in (
                "megalodon-def-project-definition",
                "megalodon-def-reduce-delta",
                "megalodon-def-reduce-beta",
                "megalodon-def-reduce-eta",
                "megalodon-def-conversion-common",
                "megalodon-def-proof",
                "megalodon-def-declarations-valid-definition",
            ):
                if required not in rendered:
                    raise SystemExit(f"Megalodon authority lacks {required}")
            positive = authority[6]
            if (
                not isinstance(positive, tuple)
                or len(positive) != 3
                or positive[0] != sx.Symbol("positive")
                or not isinstance(positive[1], tuple)
                or len(positive[1]) != 3
                or positive[1][0] != sx.Symbol("PApp")
                or positive[1][1]
                != sx.StringLiteral("MMegalodonTheoryChecks")
            ):
                raise SystemExit(
                    "Megalodon catalog lacks its theory-admission calibration"
                )
            return
    raise SystemExit("MEGALODON-TERM authority is absent")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--differential", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--positive", type=Path, required=True)
    parser.add_argument("--lean-witness", type=Path, required=True)
    args = parser.parse_args()

    checked = subprocess.run(
        [str(args.megalodon.resolve()), "-sexprinfo", str(args.positive)],
        text=True, capture_output=True, check=False,
    )
    if checked.returncode != 0:
        raise SystemExit(
            "Megalodon rejected the definition fixture:\n"
            + checked.stdout + checked.stderr
        )
    compiled = compile_document(checked.stdout)
    lean = read_witness(args.lean_witness)
    if compiled[:2] != lean:
        goal_difference = first_difference(compiled[0], lean[0])
        article_difference = first_difference(compiled[1], lean[1])
        raise SystemExit(
            "checked Megalodon definition differs from the Lean NIK witness\n"
            f"goal difference: {goal_difference}\n"
            f"article difference: {article_difference}"
        )
    check_catalog(args.catalog, *compiled[:2])
    accepted = poly.run_cetta(args.cetta, *compiled[:2])
    rejected = poly.run_cetta(args.cetta, compiled[2], compiled[1])
    validity = compile_declaration_validity(checked.stdout)
    valid_declarations = poly.run_cetta(args.cetta, validity[0], validity[1])
    invalid_declarations = poly.run_cetta(args.cetta, validity[2], validity[3])
    poly.require_public_result(accepted, accepted=True)
    poly.require_public_result(rejected, accepted=False)
    poly.require_public_result(valid_declarations, accepted=True)
    poly.require_public_result(invalid_declarations, accepted=False)
    for goal, article, accepted_result, native_status in (
        (compiled[0], compiled[1], True, "ok"),
        (compiled[2], compiled[1], False, "final-mismatch"),
        (validity[0], validity[1], True, "ok"),
        (validity[2], validity[3], False, "premise-mismatch"),
    ):
        poly.require_differential(
            args.differential, goal, article,
            accepted=accepted_result, native_status=native_status,
        )
    print(
        "(NikMegalodonDefinitionV1Summary parameters=1 definitions=1 "
        "delta=1 beta=1 paths=2 declarations-valid=1 invalid-body-rejected=1 "
        "megalodon-checked=1 lean-exact=1 cetta=1)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
