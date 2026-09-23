"""Untrusted Megalodon proof compilation for ordered NIK admission.

Compile source proof constructors in their supplied primitive, definition and
known-theorem environment. The output retains explicit conversion derivations
and ordered premises. Only the independent NIK checker establishes acceptance;
source export metadata and compiler success are not authority.
"""

from __future__ import annotations

import gslt2parse_schema_v1 as sx
import megalodon_definition_conversion_v1 as definitions
import test_nik_megalodon_polymorphic_v1 as poly


def full_environment(
    declarations: list[definitions.TermDecl], known: list[tuple[str, poly.Tm]],
    primitives: list[poly.Tp] | None = None,
) -> sx.SExpr:
    return poly.mono.app(
        "MFullEnvironment", poly.encode_primitives(primitives or []),
        definitions.encode_declarations(declarations), poly.encode_known(known),
    )


def known_member_article(
    known: list[tuple[str, poly.Tm]], identifier: str,
) -> tuple[poly.Tm, sx.SExpr]:
    for index, (candidate, proposition) in enumerate(known):
        if candidate != identifier:
            continue
        tail = known[index + 1:]
        article = poly.mono.proof_node(
            "megalodon-env-known-here",
            [poly.mono.app(identifier), poly.encode_tm(proposition),
             poly.encode_known(tail)],
            [],
        )
        selected_tail = known[index:]
        for head_identifier, head_proposition in reversed(known[:index]):
            article = poly.mono.proof_node(
                "megalodon-env-known-there",
                [poly.mono.app(head_identifier), poly.encode_tm(head_proposition),
                 poly.encode_known(selected_tail), poly.mono.app(identifier),
                 poly.encode_tm(proposition)],
                [article],
            )
            selected_tail = [(head_identifier, head_proposition), *selected_tail]
        return proposition, article
    raise SystemExit(f"unknown checked Megalodon proposition {identifier}")


def full_from_environment_article(
    declarations: list[definitions.TermDecl], known: list[tuple[str, poly.Tm]],
    type_depth: int, term_context: list[poly.Tp], proof_context: list[poly.Tm],
    source: poly.Tm, target: poly.Tm, environment_article: sx.SExpr,
) -> sx.SExpr:
    signature = [(name, value_type) for name, value_type, _ in declarations]
    _, conversion = definitions.demand_conversion_article(
        declarations, source, target
    )
    return poly.mono.proof_node(
        "megalodon-def-proof",
        [poly.mono.app("MPrimNil"),
         definitions.encode_declarations(declarations),
         poly.encode_signature(signature), poly.encode_known(known),
         poly.mono.nat(type_depth), poly.encode_type_context(term_context),
         poly.encode_proof_context(proof_context), poly.encode_tm(source),
         poly.encode_tm(target)],
        [definitions.project_signature(declarations), environment_article,
         conversion],
    )


def compile_full_proof(
    value: sx.SExpr, declarations: list[definitions.TermDecl],
    known: list[tuple[str, poly.Tm]], type_depth: int,
    term_context: list[poly.Tp], proof_context: list[poly.Tm],
    primitives: list[poly.Tp] | None = None,
) -> tuple[poly.Tm, sx.SExpr]:
    head, arguments = poly.tag(value, "Megalodon definition-aware proof")
    signature = [(name, value_type) for name, value_type, _ in declarations]
    encoded_environment = full_environment(declarations, known, primitives)
    encoded_declarations = definitions.encode_declarations(declarations)
    encoded_primitives = poly.encode_primitives(primitives or [])
    rule_prefix = "megalodon-def" if primitives is None else "megalodon-theory"

    def convert(source, target, article):
        if source == target:
            return article
        _, conversion = definitions.demand_conversion_article(declarations, source, target)
        return poly.mono.proof_node("megalodon-def-proof-convert",
            [encoded_primitives, encoded_declarations, poly.encode_known(known),
             poly.mono.nat(type_depth), poly.encode_type_context(term_context),
             poly.encode_proof_context(proof_context), poly.encode_tm(source),
             poly.encode_tm(target)], [article, conversion])

    def expose(source, article):
        target, _ = definitions.weak_head_with_article(declarations, source)
        return target, convert(source, target, article)

    if head == "HYP" and len(arguments) == 1 and isinstance(arguments[0], int):
        index = arguments[0]
        if not 0 <= index < len(proof_context):
            raise ValueError("Megalodon hypothesis index is outside its context")
        if primitives is not None:
            source = proof_context[index]
            tail = proof_context[index + 1:]
            article = poly.mono.proof_node(
                "megalodon-theory-proof-hyp-zero",
                [encoded_environment, poly.mono.nat(type_depth),
                 poly.encode_type_context(term_context), poly.encode_proof_context(tail),
                 poly.encode_tm(source)], [],
            )
            tail = [source, *tail]
            for previous in reversed(proof_context[:index]):
                article = poly.mono.proof_node(
                    "megalodon-theory-proof-hyp-succ",
                    [encoded_environment, poly.mono.nat(type_depth),
                     poly.encode_type_context(term_context), poly.encode_proof_context(tail),
                     poly.encode_tm(previous), poly.encode_tm(source)], [article],
                )
                tail = [previous, *tail]
            return source, article
        source, base_article = poly.hypothesis_proof(
            arguments[0], signature, type_depth, term_context, proof_context
        )
        environment_article = poly.mono.proof_node(
            "megalodon-env-proof-base",
            [poly.mono.app("MPrimNil"), poly.encode_signature(signature),
             poly.encode_known(known), poly.mono.nat(type_depth),
             poly.encode_type_context(term_context),
             poly.encode_proof_context(proof_context), poly.encode_tm(source)],
            [base_article],
        )
        return source, full_from_environment_article(
            declarations, known, type_depth, term_context, proof_context,
            source, source, environment_article,
        )

    if head == "KNOWN" and len(arguments) == 1 and isinstance(
        arguments[0], sx.StringLiteral
    ):
        identifier = arguments[0].text
        source, member = known_member_article(known, identifier)
        target, path = definitions.weak_head_with_article(declarations, source)
        if primitives is not None:
            return target, poly.mono.proof_node(
                "megalodon-theory-proof-known",
                [encoded_primitives, encoded_declarations, poly.encode_known(known),
                 poly.mono.nat(type_depth), poly.encode_type_context(term_context),
                 poly.encode_proof_context(proof_context), poly.mono.app(identifier),
                 poly.encode_tm(source), poly.encode_tm(target)], [member, path],
            )
        environment_article = poly.mono.proof_node(
            "megalodon-env-proof-known",
            [poly.mono.app("MPrimNil"), poly.encode_signature(signature),
             poly.encode_known(known), poly.mono.nat(type_depth),
             poly.encode_type_context(term_context),
             poly.encode_proof_context(proof_context),
             poly.mono.app(identifier), poly.encode_tm(source)],
            [member],
        )
        return target, full_from_environment_article(
            declarations, known, type_depth, term_context, proof_context,
            source, target, environment_article,
        )

    if head == "PLAM" and len(arguments) == 2:
        source_domain = poly.parse_tm(arguments[0])
        domain, domain_path = definitions.weak_head_with_article(
            declarations, source_domain
        )
        domain_type, domain_type_article = poly.type_proof(
            signature, type_depth, term_context, source_domain, primitives
        )
        if domain_type != ("prop",):
            raise SystemExit("Megalodon proof abstraction domain is not Prop")
        codomain, child = compile_full_proof(
            arguments[1], declarations, known, type_depth, term_context,
            [domain, *proof_context], primitives,
        )
        representative = poly.mono.proof_node(
            rule_prefix + "-proposition-plain",
            [*([encoded_primitives] if primitives is not None else []),
             encoded_declarations, poly.encode_signature(signature),
             poly.mono.nat(type_depth), poly.encode_type_context(term_context),
             poly.encode_tm(source_domain), poly.encode_tm(domain)],
            [definitions.project_signature(declarations), domain_type_article,
             domain_path],
        )
        result: poly.Tm = ("imp", domain, codomain)
        return result, poly.mono.proof_node(
            rule_prefix + "-proof-imp-intro",
            [encoded_primitives, encoded_declarations,
             poly.encode_signature(signature), poly.encode_known(known),
             poly.mono.nat(type_depth), poly.encode_type_context(term_context),
             poly.encode_proof_context(proof_context),
             poly.encode_tm(source_domain), poly.encode_tm(domain),
             poly.encode_tm(codomain)],
            [domain_type_article, representative, child],
        )

    if head == "PPFAP" and len(arguments) == 2:
        function, function_article = compile_full_proof(
            arguments[0], declarations, known, type_depth,
            term_context, proof_context, primitives,
        )
        argument, argument_article = compile_full_proof(
            arguments[1], declarations, known, type_depth,
            term_context, proof_context, primitives,
        )
        function, function_article = expose(function, function_article)
        if function[0] != "imp":
            raise SystemExit("Megalodon proof application has the wrong premise")
        argument_article = convert(argument, function[1], argument_article)
        argument = function[1]
        result = function[2]
        return result, poly.mono.proof_node(
            rule_prefix + "-proof-imp-elim",
            [encoded_environment, poly.mono.nat(type_depth),
             poly.encode_type_context(term_context),
             poly.encode_proof_context(proof_context),
             poly.encode_tm(argument), poly.encode_tm(result)],
            [function_article, argument_article],
        )

    if head == "TLAM" and len(arguments) == 2:
        domain = poly.parse_tp(arguments[0])
        shifted_context, shift_article = poly.shift_proof_context(
            1, 0, proof_context
        )
        body, child = compile_full_proof(
            arguments[1], declarations, known, type_depth,
            [domain, *term_context], shifted_context, primitives,
        )
        result: poly.Tm = ("all", domain, body)
        return result, poly.mono.proof_node(
            rule_prefix + "-proof-all-intro",
            [encoded_environment, poly.mono.nat(type_depth),
             poly.encode_type_context(term_context),
             poly.encode_proof_context(proof_context),
             poly.encode_proof_context(shifted_context), poly.encode_tp(domain),
             poly.encode_tm(body)],
            [poly.plain_type_proof(type_depth, domain), shift_article, child],
        )

    if head == "PTMAP" and len(arguments) == 2:
        function, function_article = compile_full_proof(
            arguments[0], declarations, known, type_depth,
            term_context, proof_context, primitives,
        )
        function, function_article = expose(function, function_article)
        if function[0] != "all":
            raise SystemExit("Megalodon proof term application targets a non-quantifier")
        argument = poly.parse_tm(arguments[1])
        argument_type, argument_type_article = poly.type_proof(
            signature, type_depth, term_context, argument, primitives
        )
        domain, body = function[1], function[2]
        if argument_type != domain:
            raise SystemExit("Megalodon proof term application has the wrong type")
        argument_representative = argument
        argument_path = definitions.path_refl(encoded_declarations, argument)
        substituted, substitution = poly.substitute(
            0, argument_representative, body
        )
        result, result_path = definitions.weak_head_with_article(
            declarations, substituted
        )
        return result, poly.mono.proof_node(
            rule_prefix + "-proof-all-elim",
            [encoded_primitives, encoded_declarations,
             poly.encode_signature(signature), poly.encode_known(known),
             poly.mono.nat(type_depth), poly.encode_type_context(term_context),
             poly.encode_proof_context(proof_context), poly.encode_tp(domain),
             poly.encode_tm(body), poly.encode_tm(argument),
             poly.encode_tm(argument_representative),
             poly.encode_tm(substituted), poly.encode_tm(result)],
            [function_article, definitions.project_signature(declarations),
             argument_type_article, argument_path, substitution, result_path],
        )

    if head == "PTPAP" and len(arguments) == 2:
        function, function_article = compile_full_proof(
            arguments[0], declarations, known, type_depth,
            term_context, proof_context, primitives,
        )
        function, function_article = expose(function, function_article)
        if function[0] != "typeAll":
            raise SystemExit("Megalodon proof type application targets a non-type-all")
        type_value = poly.parse_tp(arguments[1])
        result, substitution = poly.type_substitute_term(
            0, type_value, function[1]
        )
        return result, poly.mono.proof_node(
            rule_prefix + "-proof-type-elim",
            [encoded_environment, poly.mono.nat(type_depth),
             poly.encode_type_context(term_context),
             poly.encode_proof_context(proof_context),
             poly.encode_tm(function[1]), poly.encode_tp(type_value),
             poly.encode_tm(result)],
            [function_article, poly.plain_type_proof(type_depth, type_value),
             substitution],
        )

    raise SystemExit(
        f"unsupported checked Megalodon definition-aware proof: {sx.render(value)}"
    )


def compile_proposition(
    declarations: list[definitions.TermDecl], known: list[tuple[str, poly.Tm]],
    declared: poly.Tm, proof: sx.SExpr,
    primitives: list[poly.Tp] | None = None,
) -> tuple[sx.SExpr, sx.SExpr]:
    """Compile evidence for the stated formula in the exact source environment."""
    prefix_count = 0
    body = declared
    while body[0] == "typeAll":
        prefix_count += 1
        body = body[1]
    synthesized, article = compile_full_proof(
        proof, declarations, known, prefix_count, [], [], primitives
    )
    for depth in range(prefix_count - 1, -1, -1):
        synthesized = ("typeAll", synthesized)
        article = poly.mono.proof_node(
            ("megalodon-def" if primitives is None else "megalodon-theory") + "-proof-type-intro",
            [full_environment(declarations, known, primitives), poly.mono.nat(depth),
             poly.encode_tm(synthesized[1])],
            [article],
        )
    _, conversion = definitions.demand_conversion_article(
        declarations, synthesized, declared
    )
    article = poly.mono.proof_node(
        "megalodon-def-proof-convert",
        [poly.encode_primitives(primitives or []),
         definitions.encode_declarations(declarations), poly.encode_known(known),
         poly.mono.nat(0), poly.encode_type_context([]),
         poly.encode_proof_context([]), poly.encode_tm(synthesized),
         poly.encode_tm(declared)],
        [article, conversion],
    )
    goal = poly.mono.app(
        "MDefinitionProves", full_environment(declarations, known, primitives),
        poly.mono.nat(0), poly.encode_type_context([]),
        poly.encode_proof_context([]), poly.encode_tm(declared),
    )
    return goal, article
