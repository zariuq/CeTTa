#!/usr/bin/env python3
"""Exact Megalodon definition conversion to Lean/NIK/C differential."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

import gslt2parse_schema_v1 as sx
import test_nik_megalodon_polymorphic_v1 as poly


TermDecl = tuple[str, poly.Tp, poly.Tm | None]


def first_difference(
    left: sx.SExpr, right: sx.SExpr, path: str = "$"
) -> str | None:
    if type(left) is not type(right):
        return f"{path}: {type(left).__name__} != {type(right).__name__}"
    if isinstance(left, tuple):
        if len(left) != len(right):
            return f"{path}: tuple length {len(left)} != {len(right)}"
        for index, (left_child, right_child) in enumerate(zip(left, right)):
            difference = first_difference(
                left_child, right_child, f"{path}[{index}]"
            )
            if difference is not None:
                return difference
        return None
    if left != right:
        return f"{path}: {sx.render(left)} != {sx.render(right)}"
    return None


def encode_declarations(declarations: list[TermDecl]) -> sx.SExpr:
    result = poly.mono.app("MDeclNil")
    for name, value_type, body in reversed(declarations):
        if body is None:
            result = poly.mono.app(
                "MDeclParameter", poly.mono.app(name),
                poly.encode_tp(value_type), result,
            )
        else:
            result = poly.mono.app(
                "MDeclDefinition", poly.mono.app(name),
                poly.encode_tp(value_type), poly.encode_tm(body), result,
            )
    return result


def project_signature(declarations: list[TermDecl]) -> sx.SExpr:
    if not declarations:
        return poly.mono.proof_node("megalodon-def-project-nil", [], [])
    name, value_type, body = declarations[0]
    tail = declarations[1:]
    tail_signature = [(item_name, item_type) for item_name, item_type, _ in tail]
    if body is None:
        return poly.mono.proof_node(
            "megalodon-def-project-parameter",
            [poly.mono.app(name), poly.encode_tp(value_type),
             encode_declarations(tail), poly.encode_signature(tail_signature)],
            [project_signature(tail)],
        )
    return poly.mono.proof_node(
        "megalodon-def-project-definition",
        [poly.mono.app(name), poly.encode_tp(value_type), poly.encode_tm(body),
         encode_declarations(tail), poly.encode_signature(tail_signature)],
        [project_signature(tail)],
    )


def poly_type_article(depth: int, value_type: poly.Tp) -> sx.SExpr:
    if value_type[0] == "all":
        body = value_type[1]
        return poly.mono.proof_node(
            "megalodon-def-poly-type-all",
            [poly.mono.nat(depth), poly.encode_tp(body)],
            [poly_type_article(depth + 1, body)],
        )
    return poly.mono.proof_node(
        "megalodon-def-poly-type-plain",
        [poly.mono.nat(depth), poly.encode_tp(value_type)],
        [poly.plain_type_proof(depth, value_type)],
    )


def declarations_valid_article(declarations: list[TermDecl]) -> sx.SExpr:
    if not declarations:
        return poly.mono.proof_node(
            "megalodon-def-declarations-valid-nil", [], []
        )
    name, value_type, body = declarations[0]
    tail = declarations[1:]
    tail_signature = [(item_name, item_type) for item_name, item_type, _ in tail]
    common_arguments = [
        poly.mono.app(name), poly.encode_tp(value_type),
        encode_declarations(tail), poly.encode_signature(tail_signature),
    ]
    common_children = [
        poly_type_article(0, value_type),
        declarations_valid_article(tail),
    ]
    if body is None:
        return poly.mono.proof_node(
            "megalodon-def-declarations-valid-parameter",
            common_arguments, common_children,
        )
    inferred, body_article = poly.type_proof(tail_signature, 0, [], body)
    if inferred != value_type:
        raise SystemExit(
            f"checked Megalodon definition {name} has an invalid body type"
        )
    return poly.mono.proof_node(
        "megalodon-def-declarations-valid-definition",
        [poly.mono.app(name), poly.encode_tp(value_type), poly.encode_tm(body),
         encode_declarations(tail), poly.encode_signature(tail_signature)],
        [*common_children, body_article],
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


def path_refl(declarations: sx.SExpr, term: poly.Tm) -> sx.SExpr:
    return poly.mono.proof_node(
        "megalodon-def-path-refl", [declarations, poly.encode_tm(term)], []
    )


def path_step(
    declarations: sx.SExpr, source: poly.Tm, middle: poly.Tm,
    target: poly.Tm, step: sx.SExpr, rest: sx.SExpr,
) -> sx.SExpr:
    return poly.mono.proof_node(
        "megalodon-def-path-step",
        [declarations, poly.encode_tm(source), poly.encode_tm(middle),
         poly.encode_tm(target)],
        [step, rest],
    )


def reduce_imp_domain(
    declarations: sx.SExpr, domain: poly.Tm, result: poly.Tm,
    codomain: poly.Tm, child: sx.SExpr,
) -> sx.SExpr:
    return poly.mono.proof_node(
        "megalodon-def-reduce-imp-domain",
        [declarations, poly.encode_tm(domain), poly.encode_tm(result),
         poly.encode_tm(codomain)],
        [child],
    )


def reduce_imp_codomain(
    declarations: sx.SExpr, domain: poly.Tm, codomain: poly.Tm,
    result: poly.Tm, child: sx.SExpr,
) -> sx.SExpr:
    return poly.mono.proof_node(
        "megalodon-def-reduce-imp-codomain",
        [declarations, poly.encode_tm(domain), poly.encode_tm(codomain),
         poly.encode_tm(result)],
        [child],
    )


def definition_member_article(
    declarations: list[TermDecl], name: str,
) -> tuple[poly.Tp, poly.Tm, sx.SExpr] | None:
    for index, (candidate, value_type, body) in enumerate(declarations):
        if candidate != name:
            continue
        if body is None:
            return None
        tail = declarations[index + 1:]
        article = poly.mono.proof_node(
            "megalodon-def-member-here",
            [poly.mono.app(name), poly.encode_tp(value_type),
             poly.encode_tm(body), encode_declarations(tail)],
            [],
        )
        selected_tail = declarations[index:]
        for head_name, head_type, head_body in reversed(declarations[:index]):
            if head_body is None:
                article = poly.mono.proof_node(
                    "megalodon-def-member-there-parameter",
                    [poly.mono.app(head_name), poly.encode_tp(head_type),
                     encode_declarations(selected_tail), poly.mono.app(name),
                     poly.encode_tp(value_type), poly.encode_tm(body)],
                    [article],
                )
            else:
                article = poly.mono.proof_node(
                    "megalodon-def-member-there-definition",
                    [poly.mono.app(head_name), poly.encode_tp(head_type),
                     poly.encode_tm(head_body), encode_declarations(selected_tail),
                     poly.mono.app(name), poly.encode_tp(value_type),
                     poly.encode_tm(body)],
                    [article],
                )
            selected_tail = [
                (head_name, head_type, head_body), *selected_tail,
            ]
        return value_type, body, article
    return None


def drop_term_at(cutoff: int, term: poly.Tm) -> poly.Tm | None:
    match term:
        case ("var", index_object):
            index = int(index_object)
            if index < cutoff:
                return term
            if index == cutoff:
                return None
            return ("var", index - 1)
        case ("named", _) | ("prim", _):
            return term
        case ("app", function, argument):
            left = drop_term_at(cutoff, function)
            right = drop_term_at(cutoff, argument)
            return None if left is None or right is None else ("app", left, right)
        case ("lam", domain, body):
            result = drop_term_at(cutoff + 1, body)
            return None if result is None else ("lam", domain, result)
        case ("imp", domain, codomain):
            left = drop_term_at(cutoff, domain)
            right = drop_term_at(cutoff, codomain)
            return None if left is None or right is None else ("imp", left, right)
        case ("all", domain, body):
            result = drop_term_at(cutoff + 1, body)
            return None if result is None else ("all", domain, result)
        case ("typeApp", function, type_value):
            result = drop_term_at(cutoff, function)
            return None if result is None else ("typeApp", result, type_value)
        case ("typeLam", body):
            result = drop_term_at(cutoff, body)
            return None if result is None else ("typeLam", result)
        case ("typeAll", body):
            result = drop_term_at(cutoff, body)
            return None if result is None else ("typeAll", result)
        case _:
            raise SystemExit(f"cannot drop a Megalodon term level from {term!r}")


def reduction_once(
    declarations: list[TermDecl], source: poly.Tm,
) -> tuple[poly.Tm, sx.SExpr] | None:
    encoded_declarations = encode_declarations(declarations)
    match source:
        case ("named", name):
            member = definition_member_article(declarations, str(name))
            if member is None:
                return None
            value_type, body, member_article = member
            return body, poly.mono.proof_node(
                "megalodon-def-reduce-delta",
                [encoded_declarations, poly.mono.app(str(name)),
                 poly.encode_tp(value_type), poly.encode_tm(body)],
                [member_article],
            )
        case ("var", _) | ("prim", _):
            return None
        case ("app", function, argument):
            function_step = reduction_once(declarations, function)
            if function_step is not None:
                result, child = function_step
                target = ("app", result, argument)
                return target, poly.mono.proof_node(
                    "megalodon-def-reduce-app-function",
                    [encoded_declarations, poly.encode_tm(function),
                     poly.encode_tm(result), poly.encode_tm(argument)],
                    [child],
                )
            argument_step = reduction_once(declarations, argument)
            if argument_step is not None:
                result, child = argument_step
                target = ("app", function, result)
                return target, poly.mono.proof_node(
                    "megalodon-def-reduce-app-argument",
                    [encoded_declarations, poly.encode_tm(function),
                     poly.encode_tm(argument), poly.encode_tm(result)],
                    [child],
                )
            if function[0] == "lam":
                _, domain, body = function
                result, substitution = poly.substitute(0, argument, body)
                return result, poly.mono.proof_node(
                    "megalodon-def-reduce-beta",
                    [encoded_declarations, poly.encode_tp(domain),
                     poly.encode_tm(body), poly.encode_tm(argument),
                     poly.encode_tm(result)],
                    [substitution],
                )
            return None
        case ("lam", domain, body):
            body_step = reduction_once(declarations, body)
            if body_step is not None:
                result, child = body_step
                target = ("lam", domain, result)
                return target, poly.mono.proof_node(
                    "megalodon-def-reduce-lam-body",
                    [encoded_declarations, poly.encode_tp(domain),
                     poly.encode_tm(body), poly.encode_tm(result)],
                    [child],
                )
            if (
                body[0] == "app" and body[2] == ("var", 0)
            ):
                contracted = drop_term_at(0, body[1])
                if contracted is not None:
                    reconstructed, shift_article = poly.shift_term(
                        1, 0, contracted
                    )
                    if reconstructed != body[1]:
                        raise SystemExit("eta witness did not reconstruct its function")
                    return contracted, poly.mono.proof_node(
                        "megalodon-def-reduce-eta",
                        [encoded_declarations, poly.encode_tp(domain),
                         poly.encode_tm(body[1]), poly.encode_tm(contracted)],
                        [shift_article],
                    )
            return None
        case ("imp", domain, codomain):
            domain_step = reduction_once(declarations, domain)
            if domain_step is not None:
                result, child = domain_step
                target = ("imp", result, codomain)
                return target, reduce_imp_domain(
                    encoded_declarations, domain, result, codomain, child
                )
            codomain_step = reduction_once(declarations, codomain)
            if codomain_step is not None:
                result, child = codomain_step
                target = ("imp", domain, result)
                return target, reduce_imp_codomain(
                    encoded_declarations, domain, codomain, result, child
                )
            return None
        case ("all", domain, body):
            body_step = reduction_once(declarations, body)
            if body_step is None:
                return None
            result, child = body_step
            target = ("all", domain, result)
            return target, poly.mono.proof_node(
                "megalodon-def-reduce-all-body",
                [encoded_declarations, poly.encode_tp(domain),
                 poly.encode_tm(body), poly.encode_tm(result)],
                [child],
            )
        case ("typeApp", function, type_value):
            function_step = reduction_once(declarations, function)
            if function_step is not None:
                result, child = function_step
                target = ("typeApp", result, type_value)
                return target, poly.mono.proof_node(
                    "megalodon-def-reduce-type-app-function",
                    [encoded_declarations, poly.encode_tm(function),
                     poly.encode_tm(result), poly.encode_tp(type_value)],
                    [child],
                )
            if function[0] == "typeLam":
                body = function[1]
                result, substitution = poly.type_substitute_term(
                    0, type_value, body
                )
                return result, poly.mono.proof_node(
                    "megalodon-def-reduce-type-beta",
                    [encoded_declarations, poly.encode_tm(body),
                     poly.encode_tp(type_value), poly.encode_tm(result)],
                    [substitution],
                )
            return None
        case ("typeLam", body):
            body_step = reduction_once(declarations, body)
            if body_step is None:
                return None
            result, child = body_step
            return ("typeLam", result), poly.mono.proof_node(
                "megalodon-def-reduce-type-lam-body",
                [encoded_declarations, poly.encode_tm(body),
                 poly.encode_tm(result)],
                [child],
            )
        case ("typeAll", body):
            body_step = reduction_once(declarations, body)
            if body_step is None:
                return None
            result, child = body_step
            return ("typeAll", result), poly.mono.proof_node(
                "megalodon-def-reduce-type-all-body",
                [encoded_declarations, poly.encode_tm(body),
                 poly.encode_tm(result)],
                [child],
            )
        case _:
            raise SystemExit(f"cannot reduce Megalodon term {source!r}")


def normalize_with_article(
    declarations: list[TermDecl], source: poly.Tm, *, max_steps: int = 10000,
) -> tuple[poly.Tm, sx.SExpr]:
    steps: list[tuple[poly.Tm, poly.Tm, sx.SExpr]] = []
    current = source
    for _ in range(max_steps):
        step = reduction_once(declarations, current)
        if step is None:
            encoded_declarations = encode_declarations(declarations)
            article = path_refl(encoded_declarations, current)
            for before, after, step_article in reversed(steps):
                article = path_step(
                    encoded_declarations, before, after, current,
                    step_article, article,
                )
            return current, article
        target, step_article = step
        steps.append((current, target, step_article))
        current = target
    raise SystemExit("Megalodon normalization exceeded its certificate bound")


def conversion_article(
    declarations: list[TermDecl], left: poly.Tm, right: poly.Tm,
) -> tuple[poly.Tm, sx.SExpr]:
    left_result, left_article = normalize_with_article(declarations, left)
    right_result, right_article = normalize_with_article(declarations, right)
    if left_result != right_result:
        raise SystemExit(
            "Megalodon conversion has no common normal representative: "
            f"{left_result!r} != {right_result!r}"
        )
    encoded_declarations = encode_declarations(declarations)
    return left_result, poly.mono.proof_node(
        "megalodon-def-conversion-common",
        [encoded_declarations, poly.encode_tm(left), poly.encode_tm(right),
         poly.encode_tm(left_result)],
        [left_article, right_article],
    )


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
                or authority[3] != sx.StringLiteral("10")
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


def require_receipt(
    output: str, *, accepted: bool, native_status: str
) -> None:
    fragments = (
        "(Agreement not-run)",
        f"(NativeReplay {native_status} {'True' if accepted else 'False'} ",
        "(HornReference not-run False 0)",
        "(CompiledWorklist not-run False 0)",
        "(Outcome accepted)" if accepted else "(Outcome rejected)",
    )
    missing = [fragment for fragment in fragments if fragment not in output]
    if missing:
        receipt_start = output.rfind("(NIKReceiptV1")
        evidence = output[receipt_start:] if receipt_start >= 0 else output
        raise SystemExit(
            "CeTTa receipt lacks " + ", ".join(missing) + ":\n" + evidence
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--cetta", type=Path, required=True)
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
    require_receipt(accepted, accepted=True, native_status="ok")
    require_receipt(
        rejected, accepted=False, native_status="final-mismatch"
    )
    require_receipt(
        valid_declarations, accepted=True, native_status="ok"
    )
    require_receipt(
        invalid_declarations,
        accepted=False,
        native_status="premise-mismatch",
    )
    print(
        "(NikMegalodonDefinitionV1Summary parameters=1 definitions=1 "
        "delta=1 beta=1 paths=2 declarations-valid=1 invalid-body-rejected=1 "
        "megalodon-checked=1 lean-exact=1 cetta=1)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
