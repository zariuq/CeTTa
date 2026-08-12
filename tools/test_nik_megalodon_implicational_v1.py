#!/usr/bin/env python3
"""Paired executable canary for the NIK Megalodon implicational fragment."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess

import gslt2parse_schema_v1 as sx


def check(
    megalodon: Path, source: Path, *options: str
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(megalodon), *options, str(source)],
        text=True,
        capture_output=True,
        check=False,
    )


def list_term(*values: sx.SExpr) -> sx.SExpr:
    result: sx.SExpr = sx.Symbol("LNil")
    for value in reversed(values):
        result = (sx.Symbol("LCons"), value, result)
    return result


def proof_list(*values: sx.SExpr) -> sx.SExpr:
    result: sx.SExpr = sx.Symbol("PrNil")
    for value in reversed(values):
        result = (sx.Symbol("PrCons"), value, result)
    return result


def app(head: str, *arguments: sx.SExpr) -> sx.SExpr:
    return (sx.Symbol("PApp"), sx.StringLiteral(head), list_term(*arguments))


def rule(name: str, *arguments: sx.SExpr) -> sx.SExpr:
    return (sx.Symbol("GRuleInst"), sx.StringLiteral(name), list_term(*arguments))


def proof_node(
    name: str, arguments: list[sx.SExpr], children: list[sx.SExpr]
) -> sx.SExpr:
    return (
        sx.Symbol("GProof"),
        rule(name, *arguments),
        proof_list(*children),
    )


def encode_nat(value: int) -> sx.SExpr:
    encoded = app("MZero")
    for _ in range(value):
        encoded = app("MSucc", encoded)
    return encoded


def encode_formula(formula: tuple[object, ...]) -> sx.SExpr:
    if formula[0] == "atom":
        return app("MAtom", encode_nat(int(formula[1])))
    if formula[0] == "imp":
        return app(
            "MImp",
            encode_formula(formula[1]),
            encode_formula(formula[2]),
        )
    raise SystemExit("unknown admitted Megalodon formula")


def encode_context(context: list[tuple[str, tuple[object, ...]]]) -> sx.SExpr:
    encoded = app("MCtxNil")
    for _, formula in reversed(context):
        encoded = app("MCtxCons", encode_formula(formula), encoded)
    return encoded


def parse_formula(
    tokens: list[str], position: int, parameters: dict[str, int]
) -> tuple[int, tuple[object, ...]]:
    if position >= len(tokens):
        raise SystemExit("truncated Megalodon PFG formula")
    head = tokens[position]
    if head == "Imp":
        position, domain = parse_formula(tokens, position + 1, parameters)
        position, codomain = parse_formula(tokens, position, parameters)
        return position, ("imp", domain, codomain)
    if head not in parameters:
        raise SystemExit(
            f"Megalodon PFG formula uses unadmitted proposition {head!r}"
        )
    return position + 1, ("atom", parameters[head])


def compile_hypothesis(
    name: str, context: list[tuple[str, tuple[object, ...]]]
) -> tuple[tuple[object, ...], sx.SExpr]:
    for index, (binder, formula) in enumerate(context):
        if binder != name:
            continue
        tail = context[index + 1 :]
        article = proof_node(
            "megalodon-hyp-zero",
            [encode_context(tail), encode_formula(formula)],
            [],
        )
        for head_index in range(index - 1, -1, -1):
            prefix_tail = context[head_index + 1 :]
            article = proof_node(
                "megalodon-weaken",
                [
                    encode_context(prefix_tail),
                    encode_formula(formula),
                    encode_formula(context[head_index][1]),
                ],
                [article],
            )
        return formula, article
    raise SystemExit(
        f"Megalodon PFG proof uses nonlocal proof term {name!r}"
    )


def compile_proof(
    tokens: list[str],
    position: int,
    parameters: dict[str, int],
    context: list[tuple[str, tuple[object, ...]]],
) -> tuple[int, tuple[object, ...], sx.SExpr]:
    if position >= len(tokens):
        raise SystemExit("truncated Megalodon PFG proof")
    head = tokens[position]
    if head == "PrLa":
        if position + 1 >= len(tokens):
            raise SystemExit("truncated Megalodon PFG lambda")
        binder = tokens[position + 1]
        position, domain = parse_formula(tokens, position + 2, parameters)
        position, codomain, child = compile_proof(
            tokens, position, parameters, [(binder, domain), *context]
        )
        article = proof_node(
            "megalodon-imp-intro",
            [encode_context(context), encode_formula(domain), encode_formula(codomain)],
            [child],
        )
        return position, ("imp", domain, codomain), article
    if head == "PrAp":
        position, function_type, function = compile_proof(
            tokens, position + 1, parameters, context
        )
        position, argument_type, argument = compile_proof(
            tokens, position, parameters, context
        )
        if function_type[0] != "imp":
            raise SystemExit("Megalodon PFG applies a non-implication proof")
        domain = function_type[1]
        codomain = function_type[2]
        if argument_type != domain:
            raise SystemExit("Megalodon PFG application has the wrong domain")
        article = proof_node(
            "megalodon-imp-elim",
            [encode_context(context), encode_formula(domain), encode_formula(codomain)],
            [function, argument],
        )
        return position, codomain, article
    formula, article = compile_hypothesis(head, context)
    return position + 1, formula, article


def compile_modus_ponens_pfg(output: str) -> tuple[sx.SExpr, sx.SExpr]:
    parameter_names = re.findall(
        r"^Param [0-9a-f]{64} (\S+) : Prop$", output, re.M
    )
    if len(parameter_names) < 2 or len(set(parameter_names)) != len(
        parameter_names
    ):
        raise SystemExit("Megalodon PFG parameters left the admitted fragment")
    parameters = {name: index for index, name in enumerate(parameter_names)}
    theorem = re.search(
        r"^Thm modus_ponens : (.+)\n := (.+)$", output, re.M
    )
    if theorem is None:
        raise SystemExit("Megalodon PFG theorem left the admitted fragment")

    formula_tokens = theorem.group(1).split()
    formula_end, theorem_formula = parse_formula(formula_tokens, 0, parameters)
    if formula_end != len(formula_tokens):
        raise SystemExit("Megalodon PFG theorem has trailing formula tokens")

    proof_tokens = theorem.group(2).split()
    proof_end, inferred_formula, article = compile_proof(
        proof_tokens, 0, parameters, []
    )
    if proof_end != len(proof_tokens):
        raise SystemExit("Megalodon PFG theorem has trailing proof tokens")
    if inferred_formula != theorem_formula:
        raise SystemExit("Megalodon PFG proof does not infer its theorem formula")
    goal = app("MProves", encode_context([]), encode_formula(theorem_formula))
    return goal, article


def catalog_positive(catalog: Path) -> tuple[sx.SExpr, sx.SExpr]:
    forms = sx.parse_sexprs(
        catalog.read_text(encoding="utf-8"), source=str(catalog)
    )
    if len(forms) != 1 or not isinstance(forms[0], tuple):
        raise SystemExit("invalid NIK authority catalog")
    for authority in forms[0][1:]:
        if (
            isinstance(authority, tuple)
            and len(authority) == 7
            and authority[0] == sx.Symbol("authority")
            and authority[1] == sx.Symbol("MEGALODON-IMP")
        ):
            positive = authority[6]
            if (
                not isinstance(positive, tuple)
                or len(positive) != 3
                or positive[0] != sx.Symbol("positive")
            ):
                break
            return positive[1], positive[2]
    raise SystemExit("MEGALODON-IMP positive witness is absent")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--positive", type=Path, required=True)
    parser.add_argument("--negative", type=Path, required=True)
    args = parser.parse_args()

    positive = check(args.megalodon, args.positive, "-pfg")
    if positive.returncode != 0:
        raise SystemExit(
            "Megalodon rejected the positive implicational fixture:\n"
            + positive.stdout
            + positive.stderr
        )
    compiled = compile_modus_ponens_pfg(positive.stdout)
    if compiled != catalog_positive(args.catalog):
        raise SystemExit(
            "Megalodon PFG modus ponens does not compile to the catalog witness\n"
            f"PFG:     {sx.render(compiled[1])}\n"
            f"catalog: {sx.render(catalog_positive(args.catalog)[1])}"
        )

    negative = check(args.megalodon, args.negative)
    if negative.returncode == 0:
        raise SystemExit("Megalodon accepted the wrong-exact fixture")

    print(
        "(NikMegalodonImpV1Summary accepted=1 pfg-to-nik-exact=1 "
        "wrong-exact-rejected=1)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
