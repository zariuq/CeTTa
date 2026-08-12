#!/usr/bin/env python3
"""Exact checked Megalodon known-implication document to Lean/NIK/C."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

import gslt2parse_schema_v1 as sx
import test_nik_megalodon_polymorphic_v1 as poly


def checked_pairs(output: str) -> tuple[list[tuple[str, str, poly.Tm]], list[sx.SExpr]]:
    forms = sx.parse_sexprs(output, source="Megalodon -sexprinfo")
    theorems = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("THM")
    ]
    proofs = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("PROOF")
    ]
    expected = ("known_identity", "known_identity_reuse")
    if len(theorems) != 2 or len(proofs) != 2:
        raise SystemExit("expected two checked known-implication theorems")
    parsed: list[tuple[str, str, poly.Tm]] = []
    proof_terms: list[sx.SExpr] = []
    for theorem, proof, name in zip(theorems, proofs, expected, strict=True):
        if (
            len(theorem) != 7
            or theorem[1] != sx.StringLiteral(name)
            or not isinstance(theorem[2], sx.StringLiteral)
            or theorem[4] != 0
            or len(proof) != 3
            or proof[1] != theorem[1]
        ):
            raise SystemExit("unexpected checked known-implication theorem")
        parsed.append((name, theorem[2].text, poly.parse_tm(theorem[5])))
        proof_terms.append(proof[2])
    return parsed, proof_terms


def known_article(
    signature: list[tuple[str, poly.Tp]],
    known: list[tuple[str, poly.Tm]], identifier: str,
    proposition: poly.Tm, proof_context: list[poly.Tm],
) -> sx.SExpr:
    for index, (candidate, candidate_proposition) in enumerate(known):
        if candidate != identifier:
            continue
        if candidate_proposition != proposition:
            raise SystemExit("known identifier changed proposition")
        tail = known[index + 1:]
        member = poly.mono.proof_node(
            "megalodon-env-known-here",
            [poly.mono.app(identifier), poly.encode_tm(proposition),
             poly.encode_known(tail)],
            [],
        )
        for head_identifier, head_proposition in reversed(known[:index]):
            member = poly.mono.proof_node(
                "megalodon-env-known-there",
                [poly.mono.app(head_identifier), poly.encode_tm(head_proposition),
                 poly.encode_known(tail), poly.mono.app(identifier),
                 poly.encode_tm(proposition)],
                [member],
            )
            tail = [(head_identifier, head_proposition), *tail]
        return poly.mono.proof_node(
            "megalodon-env-proof-known",
            [poly.mono.app("MPrimNil"), poly.encode_signature(signature),
             poly.encode_known(known), poly.mono.nat(0),
             poly.encode_type_context([]), poly.encode_proof_context(proof_context),
             poly.mono.app(identifier), poly.encode_tm(proposition)],
            [member],
        )
    raise SystemExit(f"unknown checked proposition {identifier}")


def encode_environment(
    signature: list[tuple[str, poly.Tp]], known: list[tuple[str, poly.Tm]],
) -> sx.SExpr:
    return poly.mono.app(
        "MEnvironment", poly.mono.app("MPrimNil"),
        poly.encode_signature(signature), poly.encode_known(known),
    )


def environment_proof(
    value: sx.SExpr, signature: list[tuple[str, poly.Tp]],
    known: list[tuple[str, poly.Tm]],
    proof_context: list[poly.Tm],
) -> tuple[poly.Tm, sx.SExpr]:
    head, arguments = poly.tag(value, "Megalodon environment proof")
    environment = encode_environment(signature, known)
    if head == "HYP" and len(arguments) == 1 and isinstance(arguments[0], int):
        index = arguments[0]
        if index >= len(proof_context):
            raise SystemExit("nonlocal checked hypothesis")
        proposition = proof_context[index]
        base_proposition, base_article = poly.hypothesis_proof(
            index, signature, 0, [], proof_context
        )
        if base_proposition != proposition:
            raise SystemExit("hypothesis compiler changed proposition")
        return proposition, poly.mono.proof_node(
            "megalodon-env-proof-base",
            [poly.mono.app("MPrimNil"), poly.encode_signature(signature),
             poly.encode_known(known), poly.mono.nat(0),
             poly.encode_type_context([]), poly.encode_proof_context(proof_context),
             poly.encode_tm(proposition)],
            [base_article],
        )
    if head == "KNOWN" and len(arguments) == 1 and isinstance(
        arguments[0], sx.StringLiteral
    ):
        identifier = arguments[0].text
        proposition = next(
            (candidate for name, candidate in known if name == identifier), None
        )
        if proposition is None:
            raise SystemExit(f"unknown checked proposition {identifier}")
        return proposition, known_article(
            signature, known, identifier, proposition, proof_context
        )
    if head == "PLAM" and len(arguments) == 2:
        domain = poly.parse_tm(arguments[0])
        codomain, child = environment_proof(
            arguments[1], signature, known, [domain, *proof_context]
        )
        _, domain_article = poly.type_proof(signature, 0, [], domain)
        return ("imp", domain, codomain), poly.mono.proof_node(
            "megalodon-env-proof-imp-intro",
            [poly.mono.app("MPrimNil"), poly.encode_signature(signature),
             poly.encode_known(known), poly.mono.nat(0),
             poly.encode_type_context([]), poly.encode_proof_context(proof_context),
             poly.encode_tm(domain), poly.encode_tm(codomain)],
            [domain_article, child],
        )
    if head == "PPFAP" and len(arguments) == 2:
        function_type, function_article = environment_proof(
            arguments[0], signature, known, proof_context
        )
        argument_type, argument_article = environment_proof(
            arguments[1], signature, known, proof_context
        )
        if function_type[0] != "imp" or function_type[1] != argument_type:
            raise SystemExit("checked proof application has the wrong domain")
        codomain = function_type[2]
        return codomain, poly.mono.proof_node(
            "megalodon-env-proof-imp-elim",
            [environment, poly.mono.nat(0), poly.encode_type_context([]),
             poly.encode_proof_context(proof_context),
             poly.encode_tm(argument_type), poly.encode_tm(codomain)],
            [function_article, argument_article],
        )
    raise SystemExit(f"unsupported checked proof: {sx.render(value)}")


def compile_document(output: str) -> tuple[sx.SExpr, sx.SExpr, sx.SExpr]:
    forms = sx.parse_sexprs(output, source="Megalodon -sexprinfo")
    parameters = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("PARAM")
    ]
    if (
        len(parameters) != 1 or len(parameters[0]) != 6
        or not isinstance(parameters[0][2], sx.StringLiteral)
        or parameters[0][4] != (sx.Symbol("PROP"),)
    ):
        raise SystemExit("expected one checked proposition parameter")
    signature: list[tuple[str, poly.Tp]] = [
        (parameters[0][2].text, ("prop",))
    ]
    theorems, proofs = checked_pairs(output)
    (_, first_identifier, first_proposition), (
        _, second_identifier, second_proposition
    ) = theorems
    if first_identifier != second_identifier or first_proposition != second_proposition:
        raise SystemExit("reuse theorem changed its admitted proposition")
    proposition = first_proposition

    first_inferred, first_base = poly.compile_proof(
        proofs[0], signature, 0, [], []
    )
    if first_inferred != proposition:
        raise SystemExit("first proof does not infer its checked theorem")
    first_environment = poly.mono.proof_node(
        "megalodon-env-proof-base",
        [poly.mono.app("MPrimNil"), poly.encode_signature(signature),
         poly.encode_known([]), poly.mono.nat(0), poly.encode_type_context([]),
         poly.encode_proof_context([]), poly.encode_tm(proposition)],
        [first_base],
    )
    declaration = (first_identifier, proposition)
    after_first = [declaration]
    second_inferred, second_environment = environment_proof(
        proofs[1], signature, after_first, []
    )
    if second_inferred != proposition:
        raise SystemExit("known-reuse proof does not infer its checked theorem")
    final_known = [declaration, declaration]
    declarations = [declaration, declaration]
    initial_environment = encode_environment(signature, [])
    final_environment = encode_environment(signature, final_known)
    goal = poly.mono.app(
        "MMathdataChecksDocument", initial_environment,
        poly.encode_declarations(declarations), final_environment,
    )
    article = poly.mono.proof_node(
        "megalodon-env-document-cons",
        [poly.mono.app("MPrimNil"), poly.encode_signature(signature),
         poly.encode_known([]), poly.mono.app(first_identifier),
         poly.encode_tm(proposition), poly.encode_declarations([declaration]),
         final_environment],
        [first_environment, poly.mono.proof_node(
            "megalodon-env-document-cons",
            [poly.mono.app("MPrimNil"), poly.encode_signature(signature),
             poly.encode_known(after_first), poly.mono.app(second_identifier),
             poly.encode_tm(proposition), poly.encode_declarations([]),
             final_environment],
            [second_environment, poly.mono.proof_node(
                "megalodon-env-document-nil", [final_environment], []
            )],
        )],
    )
    wrong_goal = poly.mono.app(
        "MMathdataChecksDocument", initial_environment,
        poly.encode_declarations(declarations),
        encode_environment(signature, after_first),
    )
    return goal, article, wrong_goal


def read_witness(path: Path) -> tuple[sx.SExpr, sx.SExpr]:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if (
        len(forms) != 1 or not isinstance(forms[0], tuple)
        or len(forms[0]) != 3
        or forms[0][0] != sx.Symbol("nik-megalodon-known-implication-witness-v1")
    ):
        raise SystemExit("invalid Lean known-implication witness")
    return forms[0][1], forms[0][2]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--positive", type=Path, required=True)
    parser.add_argument("--lean-witness", type=Path, required=True)
    args = parser.parse_args()

    checked = subprocess.run(
        [str(args.megalodon.resolve()), "-sexprinfo", str(args.positive)],
        text=True, capture_output=True, check=False,
    )
    if checked.returncode != 0:
        raise SystemExit(checked.stderr or checked.stdout)
    compiled = compile_document(checked.stdout)
    lean = read_witness(args.lean_witness)
    if compiled[:2] != lean:
        raise SystemExit("checked Megalodon document differs from Lean NIK witness")
    accepted = poly.run_cetta(args.cetta, *compiled[:2])
    rejected = poly.run_cetta(args.cetta, compiled[2], compiled[1])
    if "Established" not in accepted or "Refuted" not in rejected:
        raise SystemExit(
            f"unexpected CeTTa results:\naccepted: {accepted}\nrejected: {rejected}"
        )
    print(
        "(NikMegalodonKnownImpV1Summary theorems=2 known=1 "
        "proof-application=1 megalodon-checked=1 lean-exact=1 cetta=1)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
