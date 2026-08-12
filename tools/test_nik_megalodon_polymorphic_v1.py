#!/usr/bin/env python3
"""Exact checked-Mathdata polymorphism to Lean/NIK/C differential."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile
from typing import TypeAlias

import gslt2parse_schema_v1 as sx
import test_nik_megalodon_term_quantified_v1 as mono


Tp: TypeAlias = tuple[object, ...]
Tm: TypeAlias = tuple[object, ...]


def tag(value: sx.SExpr, context: str) -> tuple[str, tuple[sx.SExpr, ...]]:
    if not isinstance(value, tuple) or not value:
        raise SystemExit(f"{context}: expected an S-expression")
    head = value[0]
    if not isinstance(head, sx.Symbol):
        raise SystemExit(f"{context}: expected a symbolic head")
    return head.text, value[1:]


def parse_tp(value: sx.SExpr) -> Tp:
    head, arguments = tag(value, "Megalodon type")
    if head == "TPVAR" and len(arguments) == 1 and isinstance(arguments[0], int):
        return ("var", arguments[0])
    if head == "PROP" and not arguments:
        return ("prop",)
    if head == "SET" and not arguments:
        return ("base", 0)
    if head == "BASE" and len(arguments) == 1 and isinstance(arguments[0], int):
        return ("base", arguments[0])
    if head == "AR" and len(arguments) == 2:
        return ("arr", parse_tp(arguments[0]), parse_tp(arguments[1]))
    if head == "TALL" and len(arguments) == 1:
        return ("all", parse_tp(arguments[0]))
    raise SystemExit(f"unsupported checked Megalodon type: {sx.render(value)}")


def parse_tm(value: sx.SExpr) -> Tm:
    head, arguments = tag(value, "Megalodon term")
    if head == "DB" and len(arguments) == 1 and isinstance(arguments[0], int):
        return ("var", arguments[0])
    if head == "NAME" and len(arguments) == 1 and isinstance(
        arguments[0], sx.StringLiteral
    ):
        return ("named", arguments[0].text)
    if head == "TMH" and len(arguments) == 1 and isinstance(
        arguments[0], sx.StringLiteral
    ):
        return ("named", arguments[0].text)
    if head == "PRIM" and len(arguments) == 1 and isinstance(arguments[0], int):
        return ("prim", arguments[0])
    if head == "AP" and len(arguments) == 2:
        return ("app", parse_tm(arguments[0]), parse_tm(arguments[1]))
    if head == "LAM" and len(arguments) == 2:
        return ("lam", parse_tp(arguments[0]), parse_tm(arguments[1]))
    if head == "IMP" and len(arguments) == 2:
        return ("imp", parse_tm(arguments[0]), parse_tm(arguments[1]))
    if head == "ALL" and len(arguments) == 2:
        return ("all", parse_tp(arguments[0]), parse_tm(arguments[1]))
    if head == "TPAP" and len(arguments) == 2:
        return ("typeApp", parse_tm(arguments[0]), parse_tp(arguments[1]))
    if head == "TLAM" and len(arguments) == 1:
        return ("typeLam", parse_tm(arguments[0]))
    if head == "TALL" and len(arguments) == 1:
        return ("typeAll", parse_tm(arguments[0]))
    raise SystemExit(f"unsupported checked Megalodon term: {sx.render(value)}")


def encode_tp(value: Tp) -> sx.SExpr:
    match value:
        case ("var", index):
            return mono.app("MTpVar", mono.nat(int(index)))
        case ("prop",):
            return mono.app("MTpProp")
        case ("base", index):
            return mono.app("MTpBase", mono.nat(int(index)))
        case ("arr", domain, codomain):
            return mono.app("MTpArr", encode_tp(domain), encode_tp(codomain))
        case ("all", body):
            return mono.app("MTpAll", encode_tp(body))
        case _:
            raise SystemExit(f"cannot encode Megalodon type {value!r}")


def encode_tm(value: Tm) -> sx.SExpr:
    match value:
        case ("var", index):
            return mono.app("MTmVar", mono.nat(int(index)))
        case ("named", name):
            return mono.app("MTmNamed", mono.app(str(name)))
        case ("prim", index):
            return mono.app("MTmPrim", mono.nat(int(index)))
        case ("app", function, argument):
            return mono.app("MTmApp", encode_tm(function), encode_tm(argument))
        case ("lam", domain, body):
            return mono.app("MTmLam", encode_tp(domain), encode_tm(body))
        case ("imp", domain, codomain):
            return mono.app("MTmImp", encode_tm(domain), encode_tm(codomain))
        case ("all", domain, body):
            return mono.app("MTmAll", encode_tp(domain), encode_tm(body))
        case ("typeApp", function, type_value):
            return mono.app("MTmTypeApp", encode_tm(function), encode_tp(type_value))
        case ("typeLam", body):
            return mono.app("MTmTypeLam", encode_tm(body))
        case ("typeAll", body):
            return mono.app("MTmTypeAll", encode_tm(body))
        case _:
            raise SystemExit(f"cannot encode Megalodon term {value!r}")


def encode_type_context(context: list[Tp]) -> sx.SExpr:
    result = mono.app("MTyCtxNil")
    for value in reversed(context):
        result = mono.app("MTyCtxCons", encode_tp(value), result)
    return result


def encode_proof_context(context: list[Tm]) -> sx.SExpr:
    result = mono.app("MPfCtxNil")
    for proposition in reversed(context):
        result = mono.app("MPfCtxCons", encode_tm(proposition), result)
    return result


def encode_signature(signature: list[tuple[str, Tp]]) -> sx.SExpr:
    result = mono.app("MSigNil")
    for name, value_type in reversed(signature):
        result = mono.app(
            "MSigCons", mono.app(name), encode_tp(value_type), result
        )
    return result


def encode_known(known: list[tuple[str, Tm]]) -> sx.SExpr:
    result = mono.app("MKnownNil")
    for identifier, proposition in reversed(known):
        result = mono.app(
            "MKnownCons", mono.app(identifier), encode_tm(proposition), result
        )
    return result


def encode_environment(known: list[tuple[str, Tm]]) -> sx.SExpr:
    return mono.app(
        "MEnvironment", mono.app("MPrimNil"), encode_signature([]),
        encode_known(known),
    )


def encode_declarations(declarations: list[tuple[str, Tm]]) -> sx.SExpr:
    result = mono.app("MDocumentNil")
    for identifier, proposition in reversed(declarations):
        result = mono.app(
            "MDocumentCons", mono.app(identifier), encode_tm(proposition), result
        )
    return result


def plain_type_proof(type_depth: int, value: Tp) -> sx.SExpr:
    match value:
        case ("var", index_object):
            index = int(index_object)
            if index >= type_depth:
                raise SystemExit(f"unbound Megalodon type variable {index}")
            return mono.proof_node(
                "megalodon-poly-type-var",
                [mono.nat(type_depth), mono.nat(index)],
                [mono.less_proof(index, type_depth)],
            )
        case ("prop",):
            return mono.proof_node(
                "megalodon-poly-type-prop", [mono.nat(type_depth)], []
            )
        case ("base", index):
            return mono.proof_node(
                "megalodon-poly-type-base",
                [mono.nat(type_depth), mono.nat(int(index))],
                [],
            )
        case ("arr", domain, codomain):
            return mono.proof_node(
                "megalodon-poly-type-arr",
                [mono.nat(type_depth), encode_tp(domain), encode_tp(codomain)],
                [
                    plain_type_proof(type_depth, domain),
                    plain_type_proof(type_depth, codomain),
                ],
            )
        case _:
            raise SystemExit(f"type is not plain in Megalodon: {value!r}")


def shift_term(amount: int, cutoff: int, source: Tm) -> tuple[Tm, sx.SExpr]:
    match source:
        case ("var", index_object):
            index = int(index_object)
            if cutoff == 0:
                result, child = mono.add_proof(index, amount)
                return ("var", result), mono.proof_node(
                    "megalodon-term-shift-var-zero",
                    [mono.nat(amount), mono.nat(index), mono.nat(result)],
                    [child],
                )
            if index == 0:
                return source, mono.proof_node(
                    "megalodon-term-shift-var-below",
                    [mono.nat(amount), mono.nat(cutoff - 1)],
                    [],
                )
            result, child = shift_term(amount, cutoff - 1, ("var", index - 1))
            return ("var", int(result[1]) + 1), mono.proof_node(
                "megalodon-term-shift-var-succ",
                [
                    mono.nat(amount), mono.nat(cutoff - 1),
                    mono.nat(index - 1), mono.nat(int(result[1])),
                ],
                [child],
            )
        case ("named", name):
            return source, mono.proof_node(
                "megalodon-term-shift-named",
                [mono.nat(amount), mono.nat(cutoff), mono.app(str(name))],
                [],
            )
        case ("prim", index):
            return source, mono.proof_node(
                "megalodon-env-shift-term-prim",
                [mono.nat(amount), mono.nat(cutoff), mono.nat(int(index))],
                [],
            )
        case ("app", function, argument):
            function_result, function_proof = shift_term(amount, cutoff, function)
            argument_result, argument_proof = shift_term(amount, cutoff, argument)
            return ("app", function_result, argument_result), mono.proof_node(
                "megalodon-term-shift-app",
                [
                    mono.nat(amount), mono.nat(cutoff), encode_tm(function),
                    encode_tm(argument), encode_tm(function_result),
                    encode_tm(argument_result),
                ],
                [function_proof, argument_proof],
            )
        case ("lam", domain, body):
            body_result, body_proof = shift_term(amount, cutoff + 1, body)
            return ("lam", domain, body_result), mono.proof_node(
                "megalodon-env-shift-term-lam",
                [mono.nat(amount), mono.nat(cutoff), encode_tp(domain),
                 encode_tm(body), encode_tm(body_result)],
                [body_proof],
            )
        case ("imp", domain, codomain):
            domain_result, domain_proof = shift_term(amount, cutoff, domain)
            codomain_result, codomain_proof = shift_term(amount, cutoff, codomain)
            return ("imp", domain_result, codomain_result), mono.proof_node(
                "megalodon-term-shift-imp",
                [
                    mono.nat(amount), mono.nat(cutoff), encode_tm(domain),
                    encode_tm(codomain), encode_tm(domain_result),
                    encode_tm(codomain_result),
                ],
                [domain_proof, codomain_proof],
            )
        case ("all", domain, body):
            result, child = shift_term(amount, cutoff + 1, body)
            return ("all", domain, result), mono.proof_node(
                "megalodon-term-shift-all",
                [
                    mono.nat(amount), mono.nat(cutoff), encode_tp(domain),
                    encode_tm(body), encode_tm(result),
                ],
                [child],
            )
        case ("typeApp", function, type_value):
            function_result, child = shift_term(amount, cutoff, function)
            return ("typeApp", function_result, type_value), mono.proof_node(
                "megalodon-env-shift-term-type-app",
                [mono.nat(amount), mono.nat(cutoff), encode_tm(function),
                 encode_tp(type_value), encode_tm(function_result)],
                [child],
            )
        case ("typeLam", body):
            body_result, child = shift_term(amount, cutoff, body)
            return ("typeLam", body_result), mono.proof_node(
                "megalodon-env-shift-term-type-lam",
                [mono.nat(amount), mono.nat(cutoff), encode_tm(body),
                 encode_tm(body_result)],
                [child],
            )
        case ("typeAll", body):
            body_result, child = shift_term(amount, cutoff, body)
            return ("typeAll", body_result), mono.proof_node(
                "megalodon-env-shift-term-type-all",
                [mono.nat(amount), mono.nat(cutoff), encode_tm(body),
                 encode_tm(body_result)],
                [child],
            )
        case _:
            raise SystemExit(f"cannot shift admitted Megalodon term {source!r}")


def shift_proof_context(
    amount: int, cutoff: int, context: list[Tm]
) -> tuple[list[Tm], sx.SExpr]:
    if not context:
        return [], mono.proof_node(
            "megalodon-term-shift-proof-nil",
            [mono.nat(amount), mono.nat(cutoff)],
            [],
        )
    head_result, head_proof = shift_term(amount, cutoff, context[0])
    tail_result, tail_proof = shift_proof_context(amount, cutoff, context[1:])
    return [head_result, *tail_result], mono.proof_node(
        "megalodon-term-shift-proof-cons",
        [
            mono.nat(amount), mono.nat(cutoff), encode_tm(context[0]),
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
                return result, mono.proof_node(
                    "megalodon-term-subst-var-equal",
                    [mono.nat(index), encode_tm(replacement), encode_tm(result)],
                    [child],
                )
            if variable < index:
                return body, mono.proof_node(
                    "megalodon-term-subst-var-below",
                    [mono.nat(index), encode_tm(replacement), mono.nat(variable)],
                    [mono.less_proof(variable, index)],
                )
            return ("var", variable - 1), mono.proof_node(
                "megalodon-term-subst-var-above",
                [mono.nat(index), encode_tm(replacement), mono.nat(variable - 1)],
                [mono.less_proof(index, variable)],
            )
        case ("named", name):
            return body, mono.proof_node(
                "megalodon-term-subst-named",
                [mono.nat(index), encode_tm(replacement), mono.app(str(name))],
                [],
            )
        case ("prim", primitive):
            return body, mono.proof_node(
                "megalodon-env-substitute-term-prim",
                [mono.nat(index), encode_tm(replacement), mono.nat(int(primitive))],
                [],
            )
        case ("app", function, argument):
            function_result, function_proof = substitute(index, replacement, function)
            argument_result, argument_proof = substitute(index, replacement, argument)
            return ("app", function_result, argument_result), mono.proof_node(
                "megalodon-term-subst-app",
                [
                    mono.nat(index), encode_tm(replacement), encode_tm(function),
                    encode_tm(argument), encode_tm(function_result),
                    encode_tm(argument_result),
                ],
                [function_proof, argument_proof],
            )
        case ("lam", domain, term_body):
            result, child = substitute(index + 1, replacement, term_body)
            return ("lam", domain, result), mono.proof_node(
                "megalodon-env-substitute-term-lam",
                [mono.nat(index), encode_tm(replacement), encode_tp(domain),
                 encode_tm(term_body), encode_tm(result)],
                [child],
            )
        case ("imp", domain, codomain):
            domain_result, domain_proof = substitute(index, replacement, domain)
            codomain_result, codomain_proof = substitute(index, replacement, codomain)
            return ("imp", domain_result, codomain_result), mono.proof_node(
                "megalodon-term-subst-imp",
                [
                    mono.nat(index), encode_tm(replacement), encode_tm(domain),
                    encode_tm(codomain), encode_tm(domain_result),
                    encode_tm(codomain_result),
                ],
                [domain_proof, codomain_proof],
            )
        case ("all", domain, quantified_body):
            result, child = substitute(index + 1, replacement, quantified_body)
            return ("all", domain, result), mono.proof_node(
                "megalodon-term-subst-all",
                [
                    mono.nat(index), encode_tm(replacement), encode_tp(domain),
                    encode_tm(quantified_body), encode_tm(result),
                ],
                [child],
            )
        case ("typeApp", function, type_value):
            function_result, child = substitute(index, replacement, function)
            return ("typeApp", function_result, type_value), mono.proof_node(
                "megalodon-env-substitute-term-type-app",
                [mono.nat(index), encode_tm(replacement), encode_tm(function),
                 encode_tp(type_value), encode_tm(function_result)],
                [child],
            )
        case ("typeLam", term_body):
            result, child = substitute(index, replacement, term_body)
            return ("typeLam", result), mono.proof_node(
                "megalodon-env-substitute-term-type-lam",
                [mono.nat(index), encode_tm(replacement), encode_tm(term_body),
                 encode_tm(result)],
                [child],
            )
        case ("typeAll", term_body):
            result, child = substitute(index, replacement, term_body)
            return ("typeAll", result), mono.proof_node(
                "megalodon-env-substitute-term-type-all",
                [mono.nat(index), encode_tm(replacement), encode_tm(term_body),
                 encode_tm(result)],
                [child],
            )
        case _:
            raise SystemExit(f"cannot substitute in Megalodon term {body!r}")


def shift_type(amount: int, cutoff: int, source: Tp) -> tuple[Tp, sx.SExpr]:
    match source:
        case ("var", index_object):
            index = int(index_object)
            if cutoff == 0:
                result, child = mono.add_proof(index, amount)
                return ("var", result), mono.proof_node(
                    "megalodon-env-shift-type-var-zero",
                    [mono.nat(amount), mono.nat(index), mono.nat(result)],
                    [child],
                )
            if index == 0:
                return source, mono.proof_node(
                    "megalodon-env-shift-type-var-below",
                    [mono.nat(amount), mono.nat(cutoff - 1)],
                    [],
                )
            shifted, child = shift_type(amount, cutoff - 1, ("var", index - 1))
            result = int(shifted[1]) + 1
            return ("var", result), mono.proof_node(
                "megalodon-env-shift-type-var-succ",
                [mono.nat(amount), mono.nat(cutoff - 1), mono.nat(index - 1),
                 mono.nat(result - 1)],
                [child],
            )
        case ("prop",):
            return source, mono.proof_node(
                "megalodon-env-shift-type-prop",
                [mono.nat(amount), mono.nat(cutoff)], [],
            )
        case ("base", index):
            return source, mono.proof_node(
                "megalodon-env-shift-type-base",
                [mono.nat(amount), mono.nat(cutoff), mono.nat(int(index))], [],
            )
        case ("arr", domain, codomain):
            domain_result, domain_article = shift_type(amount, cutoff, domain)
            codomain_result, codomain_article = shift_type(
                amount, cutoff, codomain
            )
            return ("arr", domain_result, codomain_result), mono.proof_node(
                "megalodon-env-shift-type-arr",
                [mono.nat(amount), mono.nat(cutoff), encode_tp(domain),
                 encode_tp(codomain), encode_tp(domain_result),
                 encode_tp(codomain_result)],
                [domain_article, codomain_article],
            )
        case ("all", body):
            body_result, child = shift_type(amount, cutoff + 1, body)
            return ("all", body_result), mono.proof_node(
                "megalodon-env-shift-type-all",
                [mono.nat(amount), mono.nat(cutoff), encode_tp(body),
                 encode_tp(body_result)],
                [child],
            )
        case _:
            raise SystemExit(f"cannot shift Megalodon type {source!r}")


def substitute_type(
    index: int, replacement: Tp, body: Tp
) -> tuple[Tp, sx.SExpr]:
    match body:
        case ("var", variable_object):
            variable = int(variable_object)
            if variable < index:
                return body, mono.proof_node(
                    "megalodon-env-substitute-type-var-below",
                    [mono.nat(index), encode_tp(replacement), mono.nat(variable)],
                    [mono.less_proof(variable, index)],
                )
            if variable == index:
                result, child = shift_type(index, 0, replacement)
                return result, mono.proof_node(
                    "megalodon-env-substitute-type-var-equal",
                    [mono.nat(index), encode_tp(replacement), encode_tp(result)],
                    [child],
                )
            result = ("var", variable - 1)
            return result, mono.proof_node(
                "megalodon-env-substitute-type-var-above",
                [mono.nat(index), encode_tp(replacement), mono.nat(variable - 1)],
                [mono.less_proof(index, variable)],
            )
        case ("prop",):
            return body, mono.proof_node(
                "megalodon-env-substitute-type-prop",
                [mono.nat(index), encode_tp(replacement)], [],
            )
        case ("base", base):
            return body, mono.proof_node(
                "megalodon-env-substitute-type-base",
                [mono.nat(index), encode_tp(replacement), mono.nat(int(base))], [],
            )
        case ("arr", domain, codomain):
            domain_result, domain_article = substitute_type(
                index, replacement, domain
            )
            codomain_result, codomain_article = substitute_type(
                index, replacement, codomain
            )
            return ("arr", domain_result, codomain_result), mono.proof_node(
                "megalodon-env-substitute-type-arr",
                [mono.nat(index), encode_tp(replacement), encode_tp(domain),
                 encode_tp(codomain), encode_tp(domain_result),
                 encode_tp(codomain_result)],
                [domain_article, codomain_article],
            )
        case ("all", quantified_body):
            result, child = substitute_type(index + 1, replacement, quantified_body)
            return ("all", result), mono.proof_node(
                "megalodon-env-substitute-type-all",
                [mono.nat(index), encode_tp(replacement), encode_tp(quantified_body),
                 encode_tp(result)],
                [child],
            )
        case _:
            raise SystemExit(f"cannot substitute in Megalodon type {body!r}")


def type_substitute_term(
    index: int, replacement: Tp, body: Tm
) -> tuple[Tm, sx.SExpr]:
    common = [mono.nat(index), encode_tp(replacement)]
    match body:
        case ("var", variable):
            return body, mono.proof_node(
                "megalodon-env-substitute-type-term-var",
                [*common, mono.nat(int(variable))], [],
            )
        case ("named", name):
            return body, mono.proof_node(
                "megalodon-env-substitute-type-term-named",
                [*common, mono.app(str(name))], [],
            )
        case ("prim", primitive):
            return body, mono.proof_node(
                "megalodon-env-substitute-type-term-prim",
                [*common, mono.nat(int(primitive))], [],
            )
        case ("app", function, argument):
            function_result, function_article = type_substitute_term(
                index, replacement, function
            )
            argument_result, argument_article = type_substitute_term(
                index, replacement, argument
            )
            return ("app", function_result, argument_result), mono.proof_node(
                "megalodon-env-substitute-type-term-app",
                [*common, encode_tm(function), encode_tm(argument),
                 encode_tm(function_result), encode_tm(argument_result)],
                [function_article, argument_article],
            )
        case ("lam", domain, term_body):
            domain_result, domain_article = substitute_type(
                index, replacement, domain
            )
            body_result, body_article = type_substitute_term(
                index, replacement, term_body
            )
            return ("lam", domain_result, body_result), mono.proof_node(
                "megalodon-env-substitute-type-term-lam",
                [*common, encode_tp(domain), encode_tm(term_body),
                 encode_tp(domain_result), encode_tm(body_result)],
                [domain_article, body_article],
            )
        case ("imp", domain, codomain):
            domain_result, domain_article = type_substitute_term(
                index, replacement, domain
            )
            codomain_result, codomain_article = type_substitute_term(
                index, replacement, codomain
            )
            return ("imp", domain_result, codomain_result), mono.proof_node(
                "megalodon-env-substitute-type-term-imp",
                [*common, encode_tm(domain), encode_tm(codomain),
                 encode_tm(domain_result), encode_tm(codomain_result)],
                [domain_article, codomain_article],
            )
        case ("all", domain, term_body):
            domain_result, domain_article = substitute_type(
                index, replacement, domain
            )
            body_result, body_article = type_substitute_term(
                index, replacement, term_body
            )
            return ("all", domain_result, body_result), mono.proof_node(
                "megalodon-env-substitute-type-term-all",
                [*common, encode_tp(domain), encode_tm(term_body),
                 encode_tp(domain_result), encode_tm(body_result)],
                [domain_article, body_article],
            )
        case ("typeApp", function, type_value):
            function_result, function_article = type_substitute_term(
                index, replacement, function
            )
            type_result, type_article = substitute_type(
                index, replacement, type_value
            )
            return ("typeApp", function_result, type_result), mono.proof_node(
                "megalodon-env-substitute-type-term-type-app",
                [*common, encode_tm(function), encode_tp(type_value),
                 encode_tm(function_result), encode_tp(type_result)],
                [function_article, type_article],
            )
        case ("typeLam", term_body):
            result, child = type_substitute_term(index + 1, replacement, term_body)
            return ("typeLam", result), mono.proof_node(
                "megalodon-env-substitute-type-term-type-lam",
                [*common, encode_tm(term_body), encode_tm(result)], [child],
            )
        case ("typeAll", term_body):
            result, child = type_substitute_term(index + 1, replacement, term_body)
            return ("typeAll", result), mono.proof_node(
                "megalodon-env-substitute-type-term-type-all",
                [*common, encode_tm(term_body), encode_tm(result)], [child],
            )
        case _:
            raise SystemExit(
                f"cannot type-substitute in Megalodon term {body!r}"
            )


def shift_type_context(
    amount: int, cutoff: int, context: list[Tp],
) -> tuple[list[Tp], sx.SExpr]:
    if not context:
        return [], mono.proof_node(
            "megalodon-def-shift-type-context-nil",
            [mono.nat(amount), mono.nat(cutoff)],
            [],
        )
    head, *tail = context
    head_result, head_article = shift_type(amount, cutoff, head)
    tail_result, tail_article = shift_type_context(amount, cutoff, tail)
    return [head_result, *tail_result], mono.proof_node(
        "megalodon-def-shift-type-context-cons",
        [mono.nat(amount), mono.nat(cutoff), encode_tp(head),
         encode_tp(head_result), encode_type_context(tail),
         encode_type_context(tail_result)],
        [head_article, tail_article],
    )


def type_proof(
    signature: list[tuple[str, Tp]], type_depth: int,
    context: list[Tp], term: Tm,
) -> tuple[Tp, sx.SExpr]:
    match term:
        case ("var", index_object):
            index = int(index_object)
            if index >= len(context):
                raise SystemExit(f"unbound Megalodon term variable {index}")
            value_type = context[index]
            article = mono.proof_node(
                "megalodon-poly-term-var-zero",
                [
                    encode_signature(signature), mono.nat(type_depth),
                    encode_type_context(context[index + 1:]),
                    encode_tp(value_type),
                ],
                [],
            )
            for head_index in range(index - 1, -1, -1):
                article = mono.proof_node(
                    "megalodon-poly-term-var-succ",
                    [
                        encode_signature(signature), mono.nat(type_depth),
                        encode_type_context(context[head_index + 1:]),
                        encode_tp(context[head_index]),
                        mono.nat(index - head_index - 1), encode_tp(value_type),
                    ],
                    [article],
                )
            return value_type, article
        case ("named", name):
            for index, (candidate, value_type) in enumerate(signature):
                if candidate != name:
                    continue
                tail = signature[index + 1:]
                article = mono.proof_node(
                    "megalodon-poly-term-named-zero",
                    [mono.app(name), encode_tp(value_type),
                     encode_signature(tail), mono.nat(type_depth),
                     encode_type_context(context)],
                    [],
                )
                tail = signature[index:]
                for head_name, head_type in reversed(signature[:index]):
                    article = mono.proof_node(
                        "megalodon-poly-term-named-succ",
                        [mono.app(head_name), encode_tp(head_type),
                         encode_signature(tail), mono.nat(type_depth),
                         encode_type_context(context), mono.app(name),
                         encode_tp(value_type)],
                        [article],
                    )
                    tail = [(head_name, head_type), *tail]
                return value_type, article
            raise SystemExit(f"unknown Megalodon term name {name}")
        case ("app", function, argument):
            function_type, function_article = type_proof(
                signature, type_depth, context, function
            )
            argument_type, argument_article = type_proof(
                signature, type_depth, context, argument
            )
            if function_type[0] != "arr" or function_type[1] != argument_type:
                raise SystemExit("Megalodon term application type mismatch")
            domain, codomain = function_type[1], function_type[2]
            return codomain, mono.proof_node(
                "megalodon-poly-term-app",
                [
                    encode_signature(signature), mono.nat(type_depth),
                    encode_type_context(context), encode_tm(function),
                    encode_tm(argument), encode_tp(domain), encode_tp(codomain),
                ],
                [function_article, argument_article],
            )
        case ("lam", domain, body):
            codomain, body_article = type_proof(
                signature, type_depth, [domain, *context], body
            )
            return ("arr", domain, codomain), mono.proof_node(
                "megalodon-poly-term-lam",
                [
                    encode_signature(signature), mono.nat(type_depth),
                    encode_type_context(context), encode_tp(domain),
                    encode_tm(body), encode_tp(codomain),
                ],
                [plain_type_proof(type_depth, domain), body_article],
            )
        case ("imp", domain, codomain):
            domain_type, domain_article = type_proof(
                signature, type_depth, context, domain
            )
            codomain_type, codomain_article = type_proof(
                signature, type_depth, context, codomain
            )
            if domain_type != ("prop",) or codomain_type != ("prop",):
                raise SystemExit("Megalodon implication operands are not propositions")
            return ("prop",), mono.proof_node(
                "megalodon-poly-term-imp",
                [
                    encode_signature(signature), mono.nat(type_depth),
                    encode_type_context(context), encode_tm(domain),
                    encode_tm(codomain),
                ],
                [domain_article, codomain_article],
            )
        case ("all", domain, body):
            body_type, body_article = type_proof(
                signature, type_depth, [domain, *context], body
            )
            if body_type != ("prop",):
                raise SystemExit("Megalodon quantified body is not a proposition")
            return ("prop",), mono.proof_node(
                "megalodon-poly-term-all",
                [
                    encode_signature(signature), mono.nat(type_depth),
                    encode_type_context(context), encode_tp(domain),
                    encode_tm(body),
                ],
                [plain_type_proof(type_depth, domain), body_article],
            )
        case ("typeApp", function, type_value):
            function_type, function_article = type_proof(
                signature, type_depth, context, function
            )
            if function_type[0] != "all":
                raise SystemExit(
                    "Megalodon term type application targets a non-polymorphic term"
                )
            body_type = function_type[1]
            result, substitution_article = substitute_type(
                0, type_value, body_type
            )
            return result, mono.proof_node(
                "megalodon-def-term-type-app",
                [encode_signature(signature), mono.nat(type_depth),
                 encode_type_context(context), encode_tm(function),
                 encode_tp(body_type), encode_tp(type_value),
                 encode_tp(result)],
                [function_article, plain_type_proof(type_depth, type_value),
                 substitution_article],
            )
        case ("typeLam", body):
            shifted_context, context_article = shift_type_context(
                1, 0, context
            )
            body_type, body_article = type_proof(
                signature, type_depth + 1, shifted_context, body
            )
            return ("all", body_type), mono.proof_node(
                "megalodon-def-term-type-lam",
                [encode_signature(signature), mono.nat(type_depth),
                 encode_type_context(context),
                 encode_type_context(shifted_context), encode_tm(body),
                 encode_tp(body_type)],
                [context_article, body_article],
            )
        case _:
            raise SystemExit(f"cannot type Megalodon term {term!r}")


def hypothesis_proof(
    index: int, signature: list[tuple[str, Tp]], type_depth: int,
    term_context: list[Tp], proof_context: list[Tm],
) -> tuple[Tm, sx.SExpr]:
    if index >= len(proof_context):
        raise SystemExit(f"unbound Megalodon proof variable {index}")
    proposition = proof_context[index]
    _, proposition_article = type_proof(
        signature, type_depth, term_context, proposition
    )
    article = mono.proof_node(
        "megalodon-poly-proof-hyp-zero",
        [
            encode_signature(signature), mono.nat(type_depth),
            encode_type_context(term_context),
            encode_proof_context(proof_context[index + 1:]),
            encode_tm(proposition),
        ],
        [proposition_article],
    )
    for head_index in range(index - 1, -1, -1):
        article = mono.proof_node(
            "megalodon-poly-proof-hyp-succ",
            [
                encode_signature(signature), mono.nat(type_depth),
                encode_type_context(term_context),
                encode_proof_context(proof_context[head_index + 1:]),
                encode_tm(proof_context[head_index]), encode_tm(proposition),
            ],
            [article],
        )
    return proposition, article


def compile_proof(
    value: sx.SExpr, signature: list[tuple[str, Tp]], type_depth: int,
    term_context: list[Tp], proof_context: list[Tm],
) -> tuple[Tm, sx.SExpr]:
    head, arguments = tag(value, "Megalodon proof")
    if head == "HYP" and len(arguments) == 1 and isinstance(arguments[0], int):
        return hypothesis_proof(
            arguments[0], signature, type_depth, term_context, proof_context
        )
    if head == "PLAM" and len(arguments) == 2:
        domain = parse_tm(arguments[0])
        codomain, child = compile_proof(
            arguments[1], signature, type_depth, term_context,
            [domain, *proof_context],
        )
        _, domain_article = type_proof(
            signature, type_depth, term_context, domain
        )
        return ("imp", domain, codomain), mono.proof_node(
            "megalodon-poly-proof-imp-intro",
            [
                encode_signature(signature), mono.nat(type_depth),
                encode_type_context(term_context),
                encode_proof_context(proof_context), encode_tm(domain),
                encode_tm(codomain),
            ],
            [domain_article, child],
        )
    if head == "TLAM" and len(arguments) == 2:
        domain = parse_tp(arguments[0])
        shifted_context, shift_article = shift_proof_context(
            1, 0, proof_context
        )
        body, child = compile_proof(
            arguments[1], signature, type_depth, [domain, *term_context],
            shifted_context,
        )
        return ("all", domain, body), mono.proof_node(
            "megalodon-poly-proof-all-intro",
            [
                encode_signature(signature), mono.nat(type_depth),
                encode_type_context(term_context),
                encode_proof_context(proof_context),
                encode_proof_context(shifted_context), encode_tp(domain),
                encode_tm(body),
            ],
            [plain_type_proof(type_depth, domain), shift_article, child],
        )
    if head == "PTMAP" and len(arguments) == 2:
        function_type, function_article = compile_proof(
            arguments[0], signature, type_depth, term_context, proof_context
        )
        if function_type[0] != "all":
            raise SystemExit("Megalodon proof term application targets a non-quantifier")
        argument = parse_tm(arguments[1])
        argument_type, argument_article = type_proof(
            signature, type_depth, term_context, argument
        )
        domain, body = function_type[1], function_type[2]
        if argument_type != domain:
            raise SystemExit("Megalodon proof term application has wrong type")
        result, substitution_article = substitute(0, argument, body)
        return result, mono.proof_node(
            "megalodon-poly-proof-all-elim",
            [
                encode_signature(signature), mono.nat(type_depth),
                encode_type_context(term_context),
                encode_proof_context(proof_context), encode_tp(domain),
                encode_tm(body), encode_tm(argument), encode_tm(result),
            ],
            [function_article, argument_article, substitution_article],
        )
    raise SystemExit(f"unsupported checked Megalodon proof: {sx.render(value)}")


def environment_proof(
    value: sx.SExpr, known: list[tuple[str, Tm]], type_depth: int,
    term_context: list[Tp], proof_context: list[Tm],
) -> tuple[Tm, sx.SExpr]:
    head, arguments = tag(value, "Megalodon environment proof")
    environment = encode_environment(known)
    if head == "KNOWN" and len(arguments) == 1 and isinstance(
        arguments[0], sx.StringLiteral
    ):
        identifier = arguments[0].text
        proposition = next(
            (candidate for name, candidate in known if name == identifier), None
        )
        if proposition is None:
            raise SystemExit(f"unknown checked Megalodon proposition {identifier}")
        tail = known[1:] if known and known[0][0] == identifier else None
        if tail is None:
            raise SystemExit("the checked reuse canary must select the newest known")
        member = mono.proof_node(
            "megalodon-env-known-here",
            [mono.app(identifier), encode_tm(proposition), encode_known(tail)],
            [],
        )
        return proposition, mono.proof_node(
            "megalodon-env-proof-known",
            [mono.app("MPrimNil"), encode_signature([]), encode_known(known),
             mono.nat(type_depth), encode_type_context(term_context),
             encode_proof_context(proof_context), mono.app(identifier),
             encode_tm(proposition)],
            [member],
        )
    if head == "PTPAP" and len(arguments) == 2:
        function_type, function_article = environment_proof(
            arguments[0], known, type_depth, term_context, proof_context
        )
        if function_type[0] != "typeAll":
            raise SystemExit("Megalodon proof type application targets a non-type-all")
        argument = parse_tp(arguments[1])
        result, substitution_article = type_substitute_term(
            0, argument, function_type[1]
        )
        return result, mono.proof_node(
            "megalodon-env-proof-type-elim",
            [environment, mono.nat(type_depth), encode_type_context(term_context),
             encode_proof_context(proof_context), encode_tm(function_type[1]),
             encode_tp(argument), encode_tm(result)],
            [function_article, substitution_article],
        )
    if head == "PTMAP" and len(arguments) == 2:
        function_type, function_article = environment_proof(
            arguments[0], known, type_depth, term_context, proof_context
        )
        if function_type[0] != "all":
            raise SystemExit("Megalodon proof term application targets a non-quantifier")
        argument = parse_tm(arguments[1])
        argument_type, argument_article = type_proof(
            [], type_depth, term_context, argument
        )
        domain, body = function_type[1], function_type[2]
        if argument_type != domain:
            raise SystemExit("Megalodon proof term application has wrong type")
        result, substitution_article = substitute(0, argument, body)
        return result, mono.proof_node(
            "megalodon-env-proof-all-elim",
            [mono.app("MPrimNil"), encode_signature([]), encode_known(known),
             mono.nat(type_depth), encode_type_context(term_context),
             encode_proof_context(proof_context), encode_tp(domain),
             encode_tm(body), encode_tm(argument), encode_tm(result)],
            [function_article, argument_article, substitution_article],
        )
    if head == "TLAM" and len(arguments) == 2:
        domain = parse_tp(arguments[0])
        shifted_context, shift_article = shift_proof_context(
            1, 0, proof_context
        )
        body, child = environment_proof(
            arguments[1], known, type_depth, [domain, *term_context],
            shifted_context,
        )
        return ("all", domain, body), mono.proof_node(
            "megalodon-env-proof-all-intro",
            [environment, mono.nat(type_depth), encode_type_context(term_context),
             encode_proof_context(proof_context),
             encode_proof_context(shifted_context), encode_tp(domain),
             encode_tm(body)],
            [plain_type_proof(type_depth, domain), shift_article, child],
        )
    raise SystemExit(
        f"unsupported checked Megalodon environment proof: {sx.render(value)}"
    )


def extract_checked_document(
    output: str,
) -> tuple[sx.SExpr, sx.SExpr, sx.SExpr]:
    forms = sx.parse_sexprs(output, source="Megalodon -sexprinfo")
    theorems = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("THM")
    ]
    proofs = [
        form for form in forms
        if isinstance(form, tuple) and form and form[0] == sx.Symbol("PROOF")
    ]
    if len(theorems) != 2 or len(proofs) != 2:
        raise SystemExit("expected exactly two checked Megalodon theorem/proof pairs")
    expected_names = ("poly_forall_identity", "poly_forall_identity_reuse")
    parsed: list[tuple[str, str, Tm, sx.SExpr]] = []
    for theorem_form, proof_form, expected_name in zip(
        theorems, proofs, expected_names, strict=True
    ):
        if (
            len(theorem_form) != 7
            or not isinstance(theorem_form[1], sx.StringLiteral)
            or theorem_form[1].text != expected_name
            or not isinstance(theorem_form[2], sx.StringLiteral)
            or theorem_form[4] != 1
            or len(proof_form) != 3
            or proof_form[1] != theorem_form[1]
        ):
            raise SystemExit("unexpected checked Megalodon polymorphic theorem")
        parsed.append(
            (expected_name, theorem_form[2].text, parse_tm(theorem_form[5]),
             proof_form[2])
        )
    _, first_identifier, first_body, first_proof = parsed[0]
    _, second_identifier, second_body, second_proof = parsed[1]
    if first_identifier != second_identifier or first_body != second_body:
        raise SystemExit("reuse theorem does not preserve the admitted proposition")
    proposition = ("typeAll", first_body)

    first_inferred, first_inner = compile_proof(first_proof, [], 1, [], [])
    if first_inferred != first_body:
        raise SystemExit("first Megalodon proof does not infer its checked theorem")
    signature = encode_signature([])
    first_base = mono.proof_node(
        "megalodon-env-proof-base",
        [mono.app("MPrimNil"), signature, encode_known([]), mono.nat(0),
         encode_type_context([]), encode_proof_context([]),
         encode_tm(proposition)],
        [mono.proof_node(
            "megalodon-poly-proof-type-intro",
            [signature, encode_tm(first_body)], [first_inner],
        )],
    )

    declaration = (first_identifier, proposition)
    after_first = [declaration]
    final_known = [declaration, declaration]
    second_inferred, second_inner = environment_proof(
        second_proof, after_first, 1, [], []
    )
    if second_inferred != second_body:
        raise SystemExit("reuse proof does not infer its checked theorem")
    second_base = mono.proof_node(
        "megalodon-env-proof-type-intro",
        [encode_environment(after_first), encode_tm(second_body)],
        [second_inner],
    )

    declarations = [declaration, declaration]
    final_environment = encode_environment(final_known)
    goal = mono.app(
        "MMathdataChecksDocument", encode_environment([]),
        encode_declarations(declarations), final_environment,
    )
    article = mono.proof_node(
        "megalodon-env-document-cons",
        [mono.app("MPrimNil"), signature, encode_known([]),
         mono.app(first_identifier), encode_tm(proposition),
         encode_declarations([declaration]), final_environment],
        [first_base, mono.proof_node(
            "megalodon-env-document-cons",
            [mono.app("MPrimNil"), signature, encode_known(after_first),
             mono.app(second_identifier), encode_tm(proposition),
             encode_declarations([]), final_environment],
            [second_base, mono.proof_node(
                "megalodon-env-document-nil", [final_environment], []
            )],
        )],
    )
    wrong_goal = mono.app(
        "MMathdataChecksDocument", encode_environment([]),
        encode_declarations(declarations), encode_environment(after_first),
    )
    return goal, article, wrong_goal


def read_lean_witness(path: Path) -> tuple[sx.SExpr, sx.SExpr]:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1 or not isinstance(forms[0], tuple):
        raise SystemExit("invalid Lean polymorphic witness")
    witness = forms[0]
    if (
        len(witness) != 3
        or witness[0] != sx.Symbol("nik-megalodon-polymorphic-witness-v1")
    ):
        raise SystemExit("wrong Lean polymorphic witness shape")
    return witness[1], witness[2]


def check_catalog(path: Path, _goal: sx.SExpr, _article: sx.SExpr) -> None:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
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
            ):
                raise SystemExit("Megalodon authority identity was not revised")
            rendered = sx.render(authority[5])
            for required in (
                "megalodon-poly-proof-all-intro",
                "megalodon-poly-proof-type-intro",
                "megalodon-env-proof-known",
                "megalodon-env-proof-type-elim",
                "megalodon-env-document-cons",
            ):
                if required not in rendered:
                    raise SystemExit(f"Megalodon authority lacks {required}")
            return
    raise SystemExit("MEGALODON-TERM authority is absent")


def run_cetta(cetta: Path, goal: sx.SExpr, article: sx.SExpr) -> str:
    cetta = cetta.resolve()
    expression = (
        "!(prime-judge &self (Check MEGALODON-TERM "
        + sx.render(goal)
        + " "
        + sx.render(article)
        + "))"
    )
    with tempfile.TemporaryDirectory(prefix="cetta-nik-") as directory:
        query = Path(directory) / "query.metta"
        query.write_text(expression + "\n", encoding="utf-8")
        result = subprocess.run(
            [str(cetta), "--lang", "prime", str(query)],
            text=True,
            capture_output=True,
            check=False,
        )
    if result.returncode != 0:
        raise SystemExit("CeTTa NIK invocation failed:\n" + result.stdout + result.stderr)
    return result.stdout


def require_receipt(output: str, *, accepted: bool) -> None:
    expected = (
        "(Agreement not-run)",
        "(NativeReplay ok True " if accepted
        else "(NativeReplay final-mismatch False ",
        "(HornReference not-run False 0)",
        "(CompiledWorklist not-run False 0)",
        "(Outcome accepted)" if accepted else "(Outcome rejected)",
    )
    missing = [fragment for fragment in expected if fragment not in output]
    if missing:
        raise SystemExit(
            "CeTTa NIK receipt lacks exact realization evidence "
            + ", ".join(missing)
            + ":\n"
            + output
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
        [str(args.megalodon), "-sexprinfo", str(args.positive)],
        text=True,
        capture_output=True,
        check=False,
    )
    if checked.returncode != 0:
        raise SystemExit(
            "Megalodon rejected the polymorphic fixture:\n"
            + checked.stdout
            + checked.stderr
        )
    compiled_goal, compiled_article, wrong_goal = extract_checked_document(
        checked.stdout
    )
    lean_goal, lean_article = read_lean_witness(args.lean_witness)
    if (compiled_goal, compiled_article) != (lean_goal, lean_article):
        raise SystemExit(
            "checked Megalodon object does not compile to the Lean NIK witness\n"
            f"Megalodon goal: {sx.render(compiled_goal)}\n"
            f"Lean goal:      {sx.render(lean_goal)}\n"
            f"Megalodon proof:{sx.render(compiled_article)}\n"
            f"Lean proof:     {sx.render(lean_article)}"
        )
    check_catalog(args.catalog, compiled_goal, compiled_article)
    accepted = run_cetta(args.cetta, compiled_goal, compiled_article)
    if "PrimeVerdict Established" not in accepted:
        raise SystemExit("CeTTa did not establish the polymorphic article:\n" + accepted)
    require_receipt(accepted, accepted=True)

    rejected = run_cetta(args.cetta, wrong_goal, compiled_article)
    if "PrimeVerdict Refuted" not in rejected:
        raise SystemExit("CeTTa did not reject the wrong polymorphic goal:\n" + rejected)
    require_receipt(rejected, accepted=False)

    print(
        "(NikMegalodonPolymorphicV1Summary "
        "theorems=2 known-reuse=1 megalodon-checked=1 lean-exact=1 "
        "cetta-established=1 "
        "wrong-goal-refuted=1 selected-realization=1)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
