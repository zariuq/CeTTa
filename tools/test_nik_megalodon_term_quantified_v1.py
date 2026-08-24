#!/usr/bin/env python3
"""Exact Megalodon term-quantifier to NIK article differential."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from typing import TypeAlias

import gslt2parse_schema_v1 as sx


Tp: TypeAlias = tuple[object, ...]
Tm: TypeAlias = tuple[object, ...]
ProofContext: TypeAlias = list[tuple[str, Tm]]


def check(megalodon: Path, source: Path, *options: str) -> subprocess.CompletedProcess[str]:
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


def proof_node(name: str, arguments: list[sx.SExpr], children: list[sx.SExpr]) -> sx.SExpr:
    instance = (
        sx.Symbol("GRuleInst"),
        sx.StringLiteral(name),
        list_term(*arguments),
    )
    return (sx.Symbol("GProof"), instance, proof_list(*children))


def nat(value: int) -> sx.SExpr:
    result = app("MNZero")
    for _ in range(value):
        result = app("MNSucc", result)
    return result


def encode_tp(value: Tp) -> sx.SExpr:
    match value:
        case ("prop",):
            return app("MTpProp")
        case ("base", index):
            return app("MTpBase", nat(int(index)))
        case ("arr", domain, codomain):
            return app("MTpArr", encode_tp(domain), encode_tp(codomain))
        case _:
            raise SystemExit(f"unsupported admitted Megalodon type {value!r}")


def encode_tm(value: Tm) -> sx.SExpr:
    match value:
        case ("var", index):
            return app("MTmVar", nat(int(index)))
        case ("named", name):
            return app("MTmNamed", app(str(name)))
        case ("app", function, argument):
            return app("MTmApp", encode_tm(function), encode_tm(argument))
        case ("imp", domain, codomain):
            return app("MTmImp", encode_tm(domain), encode_tm(codomain))
        case ("all", domain, body):
            return app("MTmAll", encode_tp(domain), encode_tm(body))
        case _:
            raise SystemExit(f"unsupported admitted Megalodon term {value!r}")


def encode_type_context(context: list[Tp]) -> sx.SExpr:
    result = app("MTyCtxNil")
    for value in reversed(context):
        result = app("MTyCtxCons", encode_tp(value), result)
    return result


def encode_proof_context(context: ProofContext) -> sx.SExpr:
    result = app("MPfCtxNil")
    for _, proposition in reversed(context):
        result = app("MPfCtxCons", encode_tm(proposition), result)
    return result


def encode_signature(signature: list[tuple[str, Tp]]) -> sx.SExpr:
    result = app("MSigNil")
    for name, value_type in reversed(signature):
        result = app("MSigCons", app(name), encode_tp(value_type), result)
    return result


def add_proof(left: int, right: int) -> tuple[int, sx.SExpr]:
    if left == 0:
        return right, proof_node("megalodon-term-add-zero", [nat(right)], [])
    result, child = add_proof(left - 1, right)
    return result + 1, proof_node(
        "megalodon-term-add-succ",
        [nat(left - 1), nat(right), nat(result)],
        [child],
    )


def less_proof(left: int, right: int) -> sx.SExpr:
    if right <= left:
        raise SystemExit(f"invalid less-than proof request: {left} < {right}")
    if left == 0:
        return proof_node("megalodon-term-less-zero", [nat(right - 1)], [])
    return proof_node(
        "megalodon-term-less-succ",
        [nat(left - 1), nat(right - 1)],
        [less_proof(left - 1, right - 1)],
    )


def shift_term(amount: int, cutoff: int, source: Tm) -> tuple[Tm, sx.SExpr]:
    match source:
        case ("var", index_object):
            index = int(index_object)
            if cutoff == 0:
                result, child = add_proof(index, amount)
                return ("var", result), proof_node(
                    "megalodon-term-shift-var-zero",
                    [nat(amount), nat(index), nat(result)],
                    [child],
                )
            if index == 0:
                return source, proof_node(
                    "megalodon-term-shift-var-below",
                    [nat(amount), nat(cutoff - 1)],
                    [],
                )
            result, child = shift_term(amount, cutoff - 1, ("var", index - 1))
            assert result[0] == "var"
            return ("var", int(result[1]) + 1), proof_node(
                "megalodon-term-shift-var-succ",
                [nat(amount), nat(cutoff - 1), nat(index - 1), nat(int(result[1]))],
                [child],
            )
        case ("named", name):
            return source, proof_node(
                "megalodon-term-shift-named",
                [nat(amount), nat(cutoff), app(str(name))],
                [],
            )
        case ("app", function, argument):
            function_result, function_proof = shift_term(amount, cutoff, function)
            argument_result, argument_proof = shift_term(amount, cutoff, argument)
            return ("app", function_result, argument_result), proof_node(
                "megalodon-term-shift-app",
                [
                    nat(amount), nat(cutoff), encode_tm(function), encode_tm(argument),
                    encode_tm(function_result), encode_tm(argument_result),
                ],
                [function_proof, argument_proof],
            )
        case ("imp", domain, codomain):
            domain_result, domain_proof = shift_term(amount, cutoff, domain)
            codomain_result, codomain_proof = shift_term(amount, cutoff, codomain)
            return ("imp", domain_result, codomain_result), proof_node(
                "megalodon-term-shift-imp",
                [
                    nat(amount), nat(cutoff), encode_tm(domain), encode_tm(codomain),
                    encode_tm(domain_result), encode_tm(codomain_result),
                ],
                [domain_proof, codomain_proof],
            )
        case ("all", domain, body):
            body_result, child = shift_term(amount, cutoff + 1, body)
            return ("all", domain, body_result), proof_node(
                "megalodon-term-shift-all",
                [nat(amount), nat(cutoff), encode_tp(domain), encode_tm(body), encode_tm(body_result)],
                [child],
            )
        case _:
            raise SystemExit(f"cannot shift unsupported Megalodon term {source!r}")


def shift_proof_context(
    amount: int, cutoff: int, context: ProofContext
) -> tuple[ProofContext, sx.SExpr]:
    if not context:
        return [], proof_node(
            "megalodon-term-shift-proof-nil", [nat(amount), nat(cutoff)], []
        )
    binder, head = context[0]
    head_result, head_proof = shift_term(amount, cutoff, head)
    tail_result, tail_proof = shift_proof_context(amount, cutoff, context[1:])
    return [(binder, head_result), *tail_result], proof_node(
        "megalodon-term-shift-proof-cons",
        [
            nat(amount), nat(cutoff), encode_tm(head),
            encode_proof_context(context[1:]), encode_tm(head_result),
            encode_proof_context(tail_result),
        ],
        [head_proof, tail_proof],
    )


def substitute(index: int, replacement: Tm, body: Tm) -> tuple[Tm, sx.SExpr]:
    match body:
        case ("var", variable_object):
            variable = int(variable_object)
            if variable == index:
                result, child = shift_term(index, 0, replacement)
                return result, proof_node(
                    "megalodon-term-subst-var-equal",
                    [nat(index), encode_tm(replacement), encode_tm(result)],
                    [child],
                )
            if variable < index:
                return body, proof_node(
                    "megalodon-term-subst-var-below",
                    [nat(index), encode_tm(replacement), nat(variable)],
                    [less_proof(variable, index)],
                )
            return ("var", variable - 1), proof_node(
                "megalodon-term-subst-var-above",
                [nat(index), encode_tm(replacement), nat(variable - 1)],
                [less_proof(index, variable)],
            )
        case ("named", name):
            return body, proof_node(
                "megalodon-term-subst-named",
                [nat(index), encode_tm(replacement), app(str(name))],
                [],
            )
        case ("app", function, argument):
            function_result, function_proof = substitute(index, replacement, function)
            argument_result, argument_proof = substitute(index, replacement, argument)
            return ("app", function_result, argument_result), proof_node(
                "megalodon-term-subst-app",
                [
                    nat(index), encode_tm(replacement), encode_tm(function),
                    encode_tm(argument), encode_tm(function_result),
                    encode_tm(argument_result),
                ],
                [function_proof, argument_proof],
            )
        case ("imp", domain, codomain):
            domain_result, domain_proof = substitute(index, replacement, domain)
            codomain_result, codomain_proof = substitute(index, replacement, codomain)
            return ("imp", domain_result, codomain_result), proof_node(
                "megalodon-term-subst-imp",
                [
                    nat(index), encode_tm(replacement), encode_tm(domain),
                    encode_tm(codomain), encode_tm(domain_result),
                    encode_tm(codomain_result),
                ],
                [domain_proof, codomain_proof],
            )
        case ("all", domain, quantified_body):
            body_result, child = substitute(index + 1, replacement, quantified_body)
            return ("all", domain, body_result), proof_node(
                "megalodon-term-subst-all",
                [
                    nat(index), encode_tm(replacement), encode_tp(domain),
                    encode_tm(quantified_body), encode_tm(body_result),
                ],
                [child],
            )
        case _:
            raise SystemExit(f"cannot substitute in unsupported term {body!r}")


def type_proof(
    signature: list[tuple[str, Tp]], context: list[Tp], term: Tm
) -> tuple[Tp, sx.SExpr]:
    match term:
        case ("var", index_object):
            index = int(index_object)
            if index >= len(context):
                raise SystemExit(f"unbound Megalodon term variable {index}")
            value_type = context[index]
            tail = context[index + 1 :]
            article = proof_node(
                "megalodon-term-type-var-zero",
                [encode_signature(signature), encode_type_context(tail), encode_tp(value_type)],
                [],
            )
            for head_index in range(index - 1, -1, -1):
                article = proof_node(
                    "megalodon-term-type-var-succ",
                    [
                        encode_signature(signature),
                        encode_type_context(context[head_index + 1 :]),
                        encode_tp(context[head_index]), nat(index - head_index - 1),
                        encode_tp(value_type),
                    ],
                    [article],
                )
            return value_type, article
        case ("named", name):
            for index, (declared_name, value_type) in enumerate(signature):
                if declared_name != name:
                    continue
                article = proof_node(
                    "megalodon-term-type-named-zero",
                    [
                        app(declared_name), encode_tp(value_type),
                        encode_signature(signature[index + 1 :]),
                        encode_type_context(context),
                    ],
                    [],
                )
                for head_index in range(index - 1, -1, -1):
                    head_name, head_type = signature[head_index]
                    article = proof_node(
                        "megalodon-term-type-named-succ",
                        [
                            app(head_name), encode_tp(head_type),
                            encode_signature(signature[head_index + 1 :]),
                            encode_type_context(context), app(declared_name),
                            encode_tp(value_type),
                        ],
                        [article],
                    )
                return value_type, article
            raise SystemExit(f"unknown Megalodon term constant {name!r}")
        case ("app", function, argument):
            function_type, function_proof = type_proof(signature, context, function)
            argument_type, argument_proof = type_proof(signature, context, argument)
            if function_type[0] != "arr" or function_type[1] != argument_type:
                raise SystemExit("Megalodon term application has the wrong argument type")
            domain, codomain = function_type[1], function_type[2]
            return codomain, proof_node(
                "megalodon-term-type-app",
                [
                    encode_signature(signature), encode_type_context(context),
                    encode_tm(function), encode_tm(argument), encode_tp(domain),
                    encode_tp(codomain),
                ],
                [function_proof, argument_proof],
            )
        case ("imp", domain, codomain):
            domain_type, domain_proof = type_proof(signature, context, domain)
            codomain_type, codomain_proof = type_proof(signature, context, codomain)
            if domain_type != ("prop",) or codomain_type != ("prop",):
                raise SystemExit("Megalodon implication operands must be propositions")
            return ("prop",), proof_node(
                "megalodon-term-type-imp",
                [
                    encode_signature(signature), encode_type_context(context),
                    encode_tm(domain), encode_tm(codomain),
                ],
                [domain_proof, codomain_proof],
            )
        case ("all", domain, quantified_body):
            body_type, body_proof = type_proof(
                signature, [domain, *context], quantified_body
            )
            if body_type != ("prop",):
                raise SystemExit("Megalodon quantified body must be a proposition")
            return ("prop",), proof_node(
                "megalodon-term-type-all",
                [
                    encode_signature(signature), encode_type_context(context),
                    encode_tp(domain), encode_tm(quantified_body),
                ],
                [body_proof],
            )
        case _:
            raise SystemExit(f"cannot type unsupported Megalodon term {term!r}")


def parse_type(tokens: list[str], position: int) -> tuple[int, Tp]:
    if position >= len(tokens):
        raise SystemExit("truncated Megalodon PFG type")
    head = tokens[position]
    if head == "set":
        return position + 1, ("base", 0)
    if head == "Prop":
        return position + 1, ("prop",)
    if head == "TpArr":
        position, domain = parse_type(tokens, position + 1)
        position, codomain = parse_type(tokens, position)
        return position, ("arr", domain, codomain)
    raise SystemExit(f"unsupported Megalodon PFG type token {head!r}")


def parse_term(
    tokens: list[str],
    position: int,
    parameters: dict[str, str],
    term_names: list[str],
) -> tuple[int, Tm]:
    if position >= len(tokens):
        raise SystemExit("truncated Megalodon PFG term")
    head = tokens[position]
    if head == "Imp":
        position, domain = parse_term(tokens, position + 1, parameters, term_names)
        position, codomain = parse_term(tokens, position, parameters, term_names)
        return position, ("imp", domain, codomain)
    if head == "All":
        if position + 1 >= len(tokens):
            raise SystemExit("truncated Megalodon PFG quantifier")
        binder = tokens[position + 1]
        position, domain = parse_type(tokens, position + 2)
        position, body = parse_term(
            tokens, position, parameters, [binder, *term_names]
        )
        return position, ("all", domain, body)
    if head == "Ap":
        position, function = parse_term(tokens, position + 1, parameters, term_names)
        position, argument = parse_term(tokens, position, parameters, term_names)
        return position, ("app", function, argument)
    if head in term_names:
        return position + 1, ("var", term_names.index(head))
    if head in parameters:
        return position + 1, ("named", parameters[head])
    raise SystemExit(f"unknown Megalodon PFG term {head!r}")


def hypothesis_proof(
    name: str,
    signature: list[tuple[str, Tp]],
    term_context: list[Tp],
    proof_context: ProofContext,
) -> tuple[Tm, sx.SExpr]:
    for index, (binder, proposition) in enumerate(proof_context):
        if binder != name:
            continue
        _, proposition_type = type_proof(signature, term_context, proposition)
        article = proof_node(
            "megalodon-term-proof-hyp-zero",
            [
                encode_signature(signature), encode_type_context(term_context),
                encode_proof_context(proof_context[index + 1 :]),
                encode_tm(proposition),
            ],
            [proposition_type],
        )
        for head_index in range(index - 1, -1, -1):
            article = proof_node(
                "megalodon-term-proof-hyp-succ",
                [
                    encode_signature(signature), encode_type_context(term_context),
                    encode_proof_context(proof_context[head_index + 1 :]),
                    encode_tm(proof_context[head_index][1]), encode_tm(proposition),
                ],
                [article],
            )
        return proposition, article
    raise SystemExit(f"nonlocal Megalodon proof variable {name!r}")


def compile_proof(
    tokens: list[str],
    position: int,
    parameters: dict[str, str],
    signature: list[tuple[str, Tp]],
    term_names: list[str],
    term_context: list[Tp],
    proof_context: ProofContext,
) -> tuple[int, Tm, sx.SExpr]:
    if position >= len(tokens):
        raise SystemExit("truncated Megalodon PFG proof")
    head = tokens[position]
    if head == "PrLa":
        if position + 1 >= len(tokens):
            raise SystemExit("truncated Megalodon proof lambda")
        binder = tokens[position + 1]
        position, domain = parse_term(tokens, position + 2, parameters, term_names)
        position, codomain, child = compile_proof(
            tokens, position, parameters, signature, term_names, term_context,
            [(binder, domain), *proof_context],
        )
        _, domain_type = type_proof(signature, term_context, domain)
        return position, ("imp", domain, codomain), proof_node(
            "megalodon-term-proof-imp-intro",
            [
                encode_signature(signature), encode_type_context(term_context),
                encode_proof_context(proof_context), encode_tm(domain),
                encode_tm(codomain),
            ],
            [domain_type, child],
        )
    if head == "PrAp":
        position, function_type, function = compile_proof(
            tokens, position + 1, parameters, signature, term_names,
            term_context, proof_context,
        )
        position, argument_type, argument = compile_proof(
            tokens, position, parameters, signature, term_names,
            term_context, proof_context,
        )
        if function_type[0] != "imp" or function_type[1] != argument_type:
            raise SystemExit("Megalodon proof application has the wrong domain")
        domain, codomain = function_type[1], function_type[2]
        return position, codomain, proof_node(
            "megalodon-term-proof-imp-elim",
            [
                encode_signature(signature), encode_type_context(term_context),
                encode_proof_context(proof_context), encode_tm(domain),
                encode_tm(codomain),
            ],
            [function, argument],
        )
    if head == "TmLa":
        if position + 1 >= len(tokens):
            raise SystemExit("truncated Megalodon term lambda")
        binder = tokens[position + 1]
        position, domain = parse_type(tokens, position + 2)
        shifted_context, shift_article = shift_proof_context(1, 0, proof_context)
        position, body, child = compile_proof(
            tokens, position, parameters, signature, [binder, *term_names],
            [domain, *term_context], shifted_context,
        )
        return position, ("all", domain, body), proof_node(
            "megalodon-term-proof-all-intro",
            [
                encode_signature(signature), encode_type_context(term_context),
                encode_proof_context(proof_context),
                encode_proof_context(shifted_context), encode_tp(domain),
                encode_tm(body),
            ],
            [shift_article, child],
        )
    if head == "TmAp":
        position, function_type, function = compile_proof(
            tokens, position + 1, parameters, signature, term_names,
            term_context, proof_context,
        )
        if function_type[0] != "all":
            raise SystemExit("Megalodon term proof application targets a non-quantifier")
        position, argument = parse_term(tokens, position, parameters, term_names)
        argument_type, argument_article = type_proof(
            signature, term_context, argument
        )
        domain, body = function_type[1], function_type[2]
        if argument_type != domain:
            raise SystemExit("Megalodon proof term application has the wrong type")
        result, substitution_article = substitute(0, argument, body)
        return position, result, proof_node(
            "megalodon-term-proof-all-elim",
            [
                encode_signature(signature), encode_type_context(term_context),
                encode_proof_context(proof_context), encode_tp(domain),
                encode_tm(body), encode_tm(argument), encode_tm(result),
            ],
            [function, argument_article, substitution_article],
        )
    proposition, article = hypothesis_proof(
        head, signature, term_context, proof_context
    )
    return position + 1, proposition, article


def compile_forall_identity_pfg(output: str) -> tuple[sx.SExpr, sx.SExpr]:
    parameter = re.search(
        r"^Param [0-9a-f]{64} (\S+) : (.+)$", output, re.M
    )
    if parameter is None:
        raise SystemExit("Megalodon PFG term parameter is absent")
    parameter_tokens = parameter.group(2).split()
    parameter_end, parameter_type = parse_type(parameter_tokens, 0)
    if parameter_end != len(parameter_tokens) or parameter_type != (
        "arr", ("base", 0), ("prop",)
    ):
        raise SystemExit("Megalodon PFG parameter left the admitted term fragment")
    parameters = {parameter.group(1): "PName"}
    signature = [("PName", parameter_type)]

    theorem = re.search(
        r"^Thm forall_identity : (.+)\n := (.+)$", output, re.M
    )
    if theorem is None:
        raise SystemExit("Megalodon PFG quantified theorem is absent")
    theorem_tokens = theorem.group(1).split()
    theorem_end, theorem_formula = parse_term(
        theorem_tokens, 0, parameters, []
    )
    if theorem_end != len(theorem_tokens):
        raise SystemExit("Megalodon PFG theorem has trailing term tokens")
    theorem_type, _ = type_proof(signature, [], theorem_formula)
    if theorem_type != ("prop",):
        raise SystemExit("Megalodon PFG theorem is not a proposition")

    proof_tokens = theorem.group(2).split()
    proof_end, inferred, article = compile_proof(
        proof_tokens, 0, parameters, signature, [], [], []
    )
    if proof_end != len(proof_tokens):
        raise SystemExit("Megalodon PFG proof has trailing tokens")
    if inferred != theorem_formula:
        raise SystemExit("Megalodon PFG proof does not infer its theorem")
    goal = app(
        "MTermProves", encode_signature(signature), encode_type_context([]),
        encode_proof_context([]), encode_tm(theorem_formula),
    )
    return goal, article


def read_lean_witness(path: Path) -> tuple[sx.SExpr, sx.SExpr]:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1 or not isinstance(forms[0], tuple):
        raise SystemExit("invalid Lean term-quantified witness")
    witness = forms[0]
    if (
        len(witness) != 3
        or witness[0] != sx.Symbol("nik-megalodon-term-witness-v1")
    ):
        raise SystemExit("wrong Lean term-quantified witness shape")
    return witness[1], witness[2]


def check_catalog(catalog: Path) -> None:
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
            and authority[1] == sx.Symbol("MEGALODON-TERM")
        ):
            if (
                authority[2] != sx.StringLiteral(
                    "megalodon.mathdata.definition-conversion"
                )
                or authority[3] != sx.StringLiteral("10")
                or "megalodon-term-proof-all-intro" not in sx.render(authority[5])
            ):
                raise SystemExit("MEGALODON-TERM does not refine the term kernel")
            return
    raise SystemExit("MEGALODON-TERM authority is absent")


def run_cetta(cetta: Path, goal: sx.SExpr, article: sx.SExpr) -> str:
    expression = (
        "!(nik:check MEGALODON-TERM "
        + sx.render(goal) + " " + sx.render(article) + ")"
    )
    result = subprocess.run(
        [str(cetta.resolve()), "--lang", "prime", "-e", expression],
        text=True, capture_output=True, check=False,
    )
    if result.returncode != 0:
        raise SystemExit("CeTTa NIK invocation failed:\n" + result.stdout + result.stderr)
    return result.stdout


def require_public_result(output: str, *, accepted: bool) -> None:
    expected = "[True]" if accepted else "[False]"
    if output.strip() != expected:
        raise SystemExit(
            f"CeTTa nik:check returned {output.strip()!r}, expected {expected}"
        )


def require_differential(
    differential: Path,
    goal: sx.SExpr,
    article: sx.SExpr,
    *,
    accepted: bool,
) -> None:
    request = (
        "(NIKDifferentialV1 MEGALODON-TERM "
        + sx.render(goal) + " " + sx.render(article) + ")"
    )
    with tempfile.TemporaryDirectory(prefix="cetta-nik-differential-") as directory:
        request_path = Path(directory) / "request.metta"
        request_path.write_text(request + "\n", encoding="utf-8")
        result = subprocess.run(
            [str(differential.resolve()), "--differential-file", str(request_path)],
            text=True,
            capture_output=True,
            check=False,
        )
    expected = "True" if accepted else "False"
    fragments = (
        f"(Outcome {'accepted' if accepted else 'rejected'})",
        "(Agreement True)",
        f"(Native {expected} ",
        f"(HornReference {expected} ",
        f"(CompiledWorklist {expected} ",
    )
    missing = [fragment for fragment in fragments if fragment not in result.stdout]
    if result.returncode != 0 or missing:
        raise SystemExit(
            "CeTTa NIK C differential qualification failed"
            + ("; missing " + ", ".join(missing) if missing else "")
            + ":\n" + result.stdout + result.stderr
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--differential", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--positive", type=Path, required=True)
    parser.add_argument("--lean-witness", type=Path, required=True)
    args = parser.parse_args()

    positive = check(args.megalodon, args.positive, "-pfg")
    if positive.returncode != 0:
        raise SystemExit(
            "Megalodon rejected the positive term-quantified fixture:\n"
            + positive.stdout
            + positive.stderr
        )
    compiled = compile_forall_identity_pfg(positive.stdout)
    expected = read_lean_witness(args.lean_witness)
    if compiled != expected:
        raise SystemExit(
            "Megalodon term-quantified PFG does not compile to the Lean witness\n"
            f"PFG goal:     {sx.render(compiled[0])}\n"
            f"Lean goal:    {sx.render(expected[0])}\n"
            f"PFG proof:    {sx.render(compiled[1])}\n"
            f"Lean proof:   {sx.render(expected[1])}"
        )
    check_catalog(args.catalog)
    accepted = run_cetta(args.cetta, compiled[0], compiled[1])
    require_public_result(accepted, accepted=True)
    require_differential(
        args.differential, compiled[0], compiled[1], accepted=True,
    )
    wrong_goal = app(
        "MTermProves", encode_signature([]), encode_type_context([]),
        encode_proof_context([]), app("MTmNamed", app("PName")),
    )
    rejected = run_cetta(args.cetta, wrong_goal, compiled[1])
    require_public_result(rejected, accepted=False)
    require_differential(
        args.differential, wrong_goal, compiled[1], accepted=False,
    )

    print(
        "(NikMegalodonTermV1Summary accepted=1 pfg-to-lean-exact=1 "
        "cetta-established=1 wrong-goal-refuted=1 selected-realization=1)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
