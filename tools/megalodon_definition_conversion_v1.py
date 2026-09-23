#!/usr/bin/env python3
"""Untrusted Megalodon conversion evidence for the existing NIK checker.

Shared generators retain exact declarations, binders and reduction steps.
Both eager normalization and demand-driven comparison produce replayable
articles; neither Python success nor a source identity certifies acceptance.
"""

from __future__ import annotations

from functools import cache

import gslt2parse_schema_v1 as sx
from nik_pattern_list_v1 import SharedPatternListEncoder
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


def _declaration_cell(row: TermDecl) -> tuple[str, tuple[sx.SExpr, ...]]:
    name, value_type, body = row
    fields = (poly.mono.app(name), poly.encode_tp(value_type))
    if body is None:
        return "MDeclParameter", fields
    return "MDeclDefinition", (*fields, poly.encode_tm(body))


_declaration_encoder = SharedPatternListEncoder("MDeclNil", _declaration_cell)


def encode_declarations(declarations: list[TermDecl]) -> sx.SExpr:
    return _declaration_encoder.encode(declarations)


def project_signature(declarations: list[TermDecl]) -> sx.SExpr:
    return _project_signature(tuple(declarations))


@cache
def _project_signature(declarations: tuple[TermDecl, ...]) -> sx.SExpr:
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
    alias_type = None
    for index, (candidate, value_type, body) in enumerate(declarations):
        if candidate != name:
            continue
        if alias_type is not None and value_type != alias_type:
            return None
        if body is None:
            return None
        # Content-addressed source aliases can publish the same identity
        # again with the body consisting solely of that identity. Select
        # the earlier same-typed body using an actual membership article;
        # a self-loop is not a useful unfolding step. Other definitions
        # and opaque declarations retain their selection boundary.
        if body == ("named", name):
            alias_type = value_type
            continue
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


class ConversionMismatch(ValueError):
    """This bounded conversion strategy found no common representative."""


def lift_reduction(declarations, parent, position, result, child):
    """Lift an actual child step through the existing reduction constructors."""
    tag = parent[0]
    if tag == "app":
        rule = "app-function" if position == 1 else "app-argument"
        arguments = ([parent[1], result, parent[2]] if position == 1
                     else [parent[1], parent[2], result])
        encoded = [poly.encode_tm(t) for t in arguments]
    elif tag == "imp":
        return (reduce_imp_domain(declarations, parent[1], result, parent[2], child)
                if position == 1 else
                reduce_imp_codomain(declarations, parent[1], parent[2], result, child))
    elif tag in {"lam", "all"}:
        rule = tag + "-body"
        encoded = [poly.encode_tp(parent[1]), poly.encode_tm(parent[2]), poly.encode_tm(result)]
    elif tag == "typeApp":
        rule = "type-app-function"
        encoded = [poly.encode_tm(parent[1]), poly.encode_tm(result), poly.encode_tp(parent[2])]
    elif tag in {"typeLam", "typeAll"}:
        rule = "type-lam-body" if tag == "typeLam" else "type-all-body"
        encoded = [poly.encode_tm(parent[1]), poly.encode_tm(result)]
    else:
        raise ConversionMismatch("not a reduction context")
    return poly.mono.proof_node("megalodon-def-reduce-" + rule,
                                [declarations, *encoded], [child])


def head_reduction_once(declarations, term):
    """Expose a head without unfolding arguments or descending under binders."""
    encoded = encode_declarations(declarations)
    if term[0] == "named":
        return reduction_once(declarations, term)
    if term[0] == "app" and term[1][0] == "lam":
        _, domain, body = term[1]
        result, substitution = poly.substitute(0, term[2], body)
        return result, poly.mono.proof_node("megalodon-def-reduce-beta",
            [encoded, poly.encode_tp(domain), poly.encode_tm(body), poly.encode_tm(term[2]),
             poly.encode_tm(result)], [substitution])
    if term[0] == "typeApp" and term[1][0] == "typeLam":
        body = term[1][1]
        result, substitution = poly.type_substitute_term(0, term[2], body)
        return result, poly.mono.proof_node("megalodon-def-reduce-type-beta",
            [encoded, poly.encode_tm(body), poly.encode_tp(term[2]), poly.encode_tm(result)],
            [substitution])
    if term[0] in {"app", "typeApp"}:
        step = head_reduction_once(declarations, term[1])
        if step is not None:
            result, child = step
            return (term[0], result, term[2]), lift_reduction(encoded, term, 1, result, child)
    if term[0] == "lam" and term[2][0] == "app" and term[2][2] == ("var", 0):
        contracted = drop_term_at(0, term[2][1])
        if contracted is not None:
            reconstructed, shift = poly.shift_term(1, 0, contracted)
            if reconstructed != term[2][1]:
                raise ConversionMismatch("eta witness does not reconstruct its function")
            return contracted, poly.mono.proof_node("megalodon-def-reduce-eta",
                [encoded, poly.encode_tp(term[1]), poly.encode_tm(term[2][1]),
                 poly.encode_tm(contracted)], [shift])
    return None


def trace_article(declarations, target, steps):
    article = path_refl(declarations, target)
    for before, after, step in reversed(steps):
        article = path_step(declarations, before, after, target, step, article)
    return article


def weak_head_with_article(declarations, term, *, max_steps=10000):
    """Compute a head representative with a replayable forward path."""
    steps, seen = [], set()
    current = term
    for _ in range(max_steps):
        if current in seen:
            raise ConversionMismatch("cyclic source head reduction")
        seen.add(current)
        step = head_reduction_once(declarations, current)
        if step is None:
            return current, trace_article(encode_declarations(declarations), current, steps)
        result, evidence = step
        steps.append((current, result, evidence))
        current = result
    raise ConversionMismatch("source head reduction exhausted its bound")


def demand_conversion_article(declarations, left, right, *, max_steps=10000):
    """Compare structurally before unfolding, retaining two checked paths.

    Equal subterms need no normalization. This matters for legal aliases whose
    source identity is shared with an earlier parameter: eager delta unfolding
    can loop although the compared occurrences are literally equal. Success
    still produces the existing NIK common-representative evidence. This is a
    bounded partial strategy, not a decision theorem for arbitrary declarations.
    """
    encoded = encode_declarations(declarations)
    remaining = max_steps

    def shared_neutral_head(a, b):
        # Congruence is a useful shortcut for the same neutral operation. It
        # must not compare the arguments of different reducible operations:
        # beta can discard them, and delta can expose a completely different
        # application spine. In that case expose heads before descending.
        while a[0] in {"app", "typeApp"}:
            a = a[1]
        while b[0] in {"app", "typeApp"}:
            b = b[1]
        return a == b and a[0] in {"var", "prim", "named"}

    def join(a, b):
        nonlocal remaining
        left_steps, right_steps, seen = [], [], set()
        while True:
            if a == b:
                return a, left_steps, right_steps
            if remaining <= 0:
                raise RuntimeError("source conversion exhausted its bound")
            remaining -= 1
            if (a, b) in seen:
                raise ConversionMismatch("cyclic source conversion")
            seen.add((a, b))
            positions = ()
            if a[0] == b[0]:
                if a[0] == "imp" or (a[0] == "app" and shared_neutral_head(a, b)):
                    positions = (1, 2)
                elif a[0] in {"lam", "all"} and a[1] == b[1]:
                    positions = (2,)
                elif a[0] == "typeApp" and a[2] == b[2] and shared_neutral_head(a, b):
                    positions = (1,)
                elif a[0] in {"typeLam", "typeAll"}:
                    positions = (1,)
            if positions:
                x, y, xs, ys = a, b, [], []
                try:
                    for position in positions:
                        common, xsteps, ysteps = join(x[position], y[position])
                        for old, new, evidence in xsteps:
                            after = (*x[:position], new, *x[position + 1:])
                            xs.append((x, after, lift_reduction(encoded, x, position, new, evidence)))
                            x = after
                        for old, new, evidence in ysteps:
                            after = (*y[:position], new, *y[position + 1:])
                            ys.append((y, after, lift_reduction(encoded, y, position, new, evidence)))
                            y = after
                    if x == y:
                        return x, left_steps + xs, right_steps + ys
                except ConversionMismatch:
                    pass
            astep = head_reduction_once(declarations, a)
            if astep is not None:
                new, evidence = astep
                left_steps.append((a, new, evidence))
                a = new
                if a == b:
                    return a, left_steps, right_steps
            bstep = head_reduction_once(declarations, b)
            if bstep is not None:
                new, evidence = bstep
                right_steps.append((b, new, evidence))
                b = new
            if astep is None and bstep is None:
                raise ConversionMismatch("source terms have no matching head or structural conversion")

    common, left_steps, right_steps = join(left, right)
    return common, poly.mono.proof_node("megalodon-def-conversion-common",
        [encoded, poly.encode_tm(left), poly.encode_tm(right), poly.encode_tm(common)],
        [trace_article(encoded, common, left_steps), trace_article(encoded, common, right_steps)])
