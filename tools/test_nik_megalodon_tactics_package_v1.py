#!/usr/bin/env python3
"""Replay the definitions of a checked Megalodon tactics package in NIK."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import subprocess
import tempfile

import gslt2parse_schema_v1 as sx
import nik_proof_dag_v1 as proof_dag
import test_nik_megalodon_definition_conversion_v1 as definitions
import test_nik_megalodon_polymorphic_v1 as poly


TermDecl = tuple[str, str, poly.Tp, poly.Tm, int]
CheckedTheorem = tuple[str, str, int, poly.Tm, sx.SExpr, int]


@dataclass(slots=True)
class DAGTotals:
    raw_nodes: int = 0
    unique_nodes: int = 0
    raw_patterns: int = 0
    unique_patterns: int = 0
    raw_bytes: int = 0
    shared_bytes: int = 0
    shared_checks: int = 0

    def add(
        self, compilation: proof_dag.SharedDAGCompilation,
        raw_bytes: int, shared_bytes: int,
    ) -> None:
        self.raw_nodes += compilation.raw_proof_nodes
        self.unique_nodes += compilation.unique_proof_nodes
        self.raw_patterns += compilation.raw_pattern_occurrences
        self.unique_patterns += compilation.unique_pattern_nodes
        self.raw_bytes += raw_bytes
        self.shared_bytes += shared_bytes
        self.shared_checks += int(
            compilation.shared_proof_occurrences > 0
            or compilation.shared_pattern_occurrences > 0
        )


def wrap_prefix_polymorphism(
    count: int, value_type: poly.Tp, body: poly.Tm,
) -> tuple[poly.Tp, poly.Tm]:
    for _ in range(count):
        value_type = ("all", value_type)
        body = ("typeLam", body)
    return value_type, body


def checked_definitions(output: str) -> list[TermDecl]:
    forms = sx.parse_sexprs(output, source="Megalodon -sexprinfo")
    result: list[TermDecl] = []
    theorem_count = 0
    qed_count = 0
    for form in forms:
        if not isinstance(form, tuple) or not form:
            continue
        if form[0] == sx.Symbol("THM"):
            theorem_count += 1
        elif form[0] == sx.Symbol("QED"):
            qed_count += 1
        elif form[0] == sx.Symbol("DEF"):
            if (
                len(form) != 7
                or not isinstance(form[1], sx.StringLiteral)
                or not isinstance(form[2], sx.StringLiteral)
                or not isinstance(form[3], int)
            ):
                raise SystemExit("malformed checked Megalodon definition")
            value_type, body = wrap_prefix_polymorphism(
                form[3], poly.parse_tp(form[4]), poly.parse_tm(form[5])
            )
            result.append(
                (form[1].text, form[2].text, value_type, body, form[3])
            )
    if len(result) != 8 or theorem_count != 10 or qed_count != 10:
        raise SystemExit(
            "the checked tactics package changed shape: "
            f"definitions={len(result)} theorems={theorem_count} qeds={qed_count}"
        )
    identifiers = [identifier for _, identifier, _, _, _ in result]
    if len(set(identifiers)) != len(identifiers):
        raise SystemExit("the checked package contains duplicate term identities")
    return result


def checked_theorems(output: str) -> list[CheckedTheorem]:
    forms = sx.parse_sexprs(output, source="Megalodon -sexprinfo")
    result: list[CheckedTheorem] = []
    pending: tuple[str, str, int, poly.Tm, int] | None = None
    definition_count = 0
    for form in forms:
        if not isinstance(form, tuple) or not form:
            continue
        if form[0] == sx.Symbol("DEF"):
            definition_count += 1
        elif form[0] == sx.Symbol("THM"):
            if (
                pending is not None or len(form) != 7
                or not isinstance(form[1], sx.StringLiteral)
                or not isinstance(form[2], sx.StringLiteral)
                or not isinstance(form[4], int)
            ):
                raise SystemExit("malformed checked Megalodon theorem")
            pending = (
                form[1].text, form[2].text, form[4],
                poly.parse_tm(form[5]), definition_count,
            )
        elif form[0] == sx.Symbol("PROOF"):
            if (
                pending is None or len(form) != 3
                or form[1] != sx.StringLiteral(pending[0])
            ):
                raise SystemExit("orphan checked Megalodon proof")
            result.append((*pending[:4], form[2], pending[4]))
            pending = None
    if pending is not None or len(result) != 10:
        raise SystemExit("checked Megalodon tactics theorems changed shape")
    return result


def term_typing_goal(
    signature: list[tuple[str, poly.Tp]], context: list[poly.Tp],
    term: poly.Tm, value_type: poly.Tp,
) -> sx.SExpr:
    return poly.mono.app(
        "MPolyTermHasType", poly.encode_signature(signature),
        poly.mono.nat(0), poly.encode_type_context(context),
        poly.encode_tm(term), poly.encode_tp(value_type),
    )


def retained_declarations(values: list[TermDecl]) -> list[definitions.TermDecl]:
    return [
        (identifier, value_type, body)
        for _, identifier, value_type, body, _ in reversed(values)
    ]


def full_environment(
    declarations: list[definitions.TermDecl], known: list[tuple[str, poly.Tm]],
) -> sx.SExpr:
    return poly.mono.app(
        "MFullEnvironment", poly.mono.app("MPrimNil"),
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
    _, conversion = definitions.conversion_article(
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
) -> tuple[poly.Tm, sx.SExpr]:
    head, arguments = poly.tag(value, "Megalodon definition-aware proof")
    signature = [(name, value_type) for name, value_type, _ in declarations]
    encoded_environment = full_environment(declarations, known)
    encoded_declarations = definitions.encode_declarations(declarations)

    if head == "HYP" and len(arguments) == 1 and isinstance(arguments[0], int):
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
        target, _ = definitions.normalize_with_article(declarations, source)
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
        domain, domain_path = definitions.normalize_with_article(
            declarations, source_domain
        )
        domain_type, domain_type_article = poly.type_proof(
            signature, type_depth, term_context, source_domain
        )
        if domain_type != ("prop",):
            raise SystemExit("Megalodon proof abstraction domain is not Prop")
        codomain, child = compile_full_proof(
            arguments[1], declarations, known, type_depth, term_context,
            [domain, *proof_context],
        )
        representative = poly.mono.proof_node(
            "megalodon-def-proposition-plain",
            [encoded_declarations, poly.encode_signature(signature),
             poly.mono.nat(type_depth), poly.encode_type_context(term_context),
             poly.encode_tm(source_domain), poly.encode_tm(domain)],
            [definitions.project_signature(declarations), domain_type_article,
             domain_path],
        )
        result: poly.Tm = ("imp", domain, codomain)
        return result, poly.mono.proof_node(
            "megalodon-def-proof-imp-intro",
            [poly.mono.app("MPrimNil"), encoded_declarations,
             poly.encode_signature(signature), poly.encode_known(known),
             poly.mono.nat(type_depth), poly.encode_type_context(term_context),
             poly.encode_proof_context(proof_context),
             poly.encode_tm(source_domain), poly.encode_tm(domain),
             poly.encode_tm(codomain)],
            [representative, child],
        )

    if head == "PPFAP" and len(arguments) == 2:
        function, function_article = compile_full_proof(
            arguments[0], declarations, known, type_depth,
            term_context, proof_context,
        )
        argument, argument_article = compile_full_proof(
            arguments[1], declarations, known, type_depth,
            term_context, proof_context,
        )
        if function[0] != "imp" or function[1] != argument:
            raise SystemExit("Megalodon proof application has the wrong premise")
        result = function[2]
        return result, poly.mono.proof_node(
            "megalodon-def-proof-imp-elim",
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
            [domain, *term_context], shifted_context,
        )
        result: poly.Tm = ("all", domain, body)
        return result, poly.mono.proof_node(
            "megalodon-def-proof-all-intro",
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
            term_context, proof_context,
        )
        if function[0] != "all":
            raise SystemExit("Megalodon proof term application targets a non-quantifier")
        argument = poly.parse_tm(arguments[1])
        argument_type, argument_type_article = poly.type_proof(
            signature, type_depth, term_context, argument
        )
        domain, body = function[1], function[2]
        if argument_type != domain:
            raise SystemExit("Megalodon proof term application has the wrong type")
        argument_representative, argument_path = definitions.normalize_with_article(
            declarations, argument
        )
        substituted, substitution = poly.substitute(
            0, argument_representative, body
        )
        result, result_path = definitions.normalize_with_article(
            declarations, substituted
        )
        return result, poly.mono.proof_node(
            "megalodon-def-proof-all-elim",
            [poly.mono.app("MPrimNil"), encoded_declarations,
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
            term_context, proof_context,
        )
        if function[0] != "typeAll":
            raise SystemExit("Megalodon proof type application targets a non-type-all")
        type_value = poly.parse_tp(arguments[1])
        result, substitution = poly.type_substitute_term(
            0, type_value, function[1]
        )
        return result, poly.mono.proof_node(
            "megalodon-def-proof-type-elim",
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


def compile_theorem(
    theorem: CheckedTheorem, definitions_in_scope: list[TermDecl],
    known: list[tuple[str, poly.Tm]],
) -> tuple[str, poly.Tm, sx.SExpr, sx.SExpr]:
    label, identifier, prefix_count, source_body, proof, _ = theorem
    declarations = retained_declarations(definitions_in_scope)
    synthesized, article = compile_full_proof(
        proof, declarations, known, prefix_count, [], []
    )
    for depth in range(prefix_count - 1, -1, -1):
        synthesized = ("typeAll", synthesized)
        article = poly.mono.proof_node(
            "megalodon-def-proof-type-intro",
            [full_environment(declarations, known), poly.mono.nat(depth),
             poly.encode_tm(synthesized[1])],
            [article],
        )
    declared = source_body
    for _ in range(prefix_count):
        declared = ("typeAll", declared)
    _, conversion = definitions.conversion_article(
        declarations, synthesized, declared
    )
    article = poly.mono.proof_node(
        "megalodon-def-proof-convert",
        [poly.mono.app("MPrimNil"),
         definitions.encode_declarations(declarations), poly.encode_known(known),
         poly.mono.nat(0), poly.encode_type_context([]),
         poly.encode_proof_context([]), poly.encode_tm(synthesized),
         poly.encode_tm(declared)],
        [article, conversion],
    )
    goal = poly.mono.app(
        "MDefinitionProves", full_environment(declarations, known),
        poly.mono.nat(0), poly.encode_type_context([]),
        poly.encode_proof_context([]), poly.encode_tm(declared),
    )
    return label, declared, goal, article


def require_receipt(
    output: str, *, accepted: bool, native_status: str,
) -> None:
    definitions.require_receipt(
        output, accepted=accepted, native_status=native_status
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
            [str(differential.resolve()), "--differential-file",
             str(request_path)],
            text=True, capture_output=True, check=False,
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
            "CeTTa NIK differential qualification failed"
            + ("; missing " + ", ".join(missing) if missing else "")
            + ":\n" + result.stdout + result.stderr
        )


def check_both(
    cetta: Path,
    differential: Path,
    totals: DAGTotals,
    goal: sx.SExpr,
    article: sx.SExpr,
    *,
    accepted: bool,
    native_status: str,
) -> None:
    compilation = proof_dag.compile_shared_article(goal, article)
    raw_bytes = len(sx.render(article).encode("utf-8"))
    shared_bytes = len(sx.render(compilation.article).encode("utf-8"))
    receipt = poly.run_cetta(cetta, goal, compilation.article)
    require_receipt(
        receipt, accepted=accepted, native_status=native_status
    )
    totals.add(compilation, raw_bytes, shared_bytes)
    require_differential(
        differential, goal, article, accepted=accepted
    )


def forged_context_shift() -> tuple[sx.SExpr, sx.SExpr]:
    proposition: poly.Tp = ("prop",)
    forged_type: poly.Tp = ("var", 0)
    source_context = [proposition]
    forged_context = [forged_type]
    body: poly.Tm = ("var", 0)
    term: poly.Tm = ("typeLam", body)

    false_head_shift = poly.mono.proof_node(
        "megalodon-env-shift-type-prop",
        [poly.mono.nat(1), poly.mono.nat(0)],
        [],
    )
    tail_shift = poly.mono.proof_node(
        "megalodon-def-shift-type-context-nil",
        [poly.mono.nat(1), poly.mono.nat(0)],
        [],
    )
    false_context_shift = poly.mono.proof_node(
        "megalodon-def-shift-type-context-cons",
        [poly.mono.nat(1), poly.mono.nat(0), poly.encode_tp(proposition),
         poly.encode_tp(forged_type), poly.encode_type_context([]),
         poly.encode_type_context([])],
        [false_head_shift, tail_shift],
    )
    body_article = poly.mono.proof_node(
        "megalodon-poly-term-var-zero",
        [poly.encode_signature([]), poly.mono.nat(1),
         poly.encode_type_context([]), poly.encode_tp(forged_type)],
        [],
    )
    article = poly.mono.proof_node(
        "megalodon-def-term-type-lam",
        [poly.encode_signature([]), poly.mono.nat(0),
         poly.encode_type_context(source_context),
         poly.encode_type_context(forged_context), poly.encode_tm(body),
         poly.encode_tp(forged_type)],
        [false_context_shift, body_article],
    )
    goal = term_typing_goal(
        [], source_context, term, ("all", forged_type)
    )
    return goal, article


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--differential", type=Path, required=True)
    parser.add_argument("--positive", type=Path, required=True)
    args = parser.parse_args()

    checked = subprocess.run(
        [str(args.megalodon.resolve()), "-sexprinfo", str(args.positive)],
        text=True, capture_output=True, check=False,
    )
    if checked.returncode != 0:
        raise SystemExit(
            "Megalodon rejected the tactics package:\n"
            + checked.stdout + checked.stderr
        )

    all_definitions = checked_definitions(checked.stdout)
    signature: list[tuple[str, poly.Tp]] = []
    dag_totals = DAGTotals()
    checks = 0
    polymorphic = 0
    for label, identifier, value_type, body, prefix_count in all_definitions:
        inferred, body_article = poly.type_proof(signature, 0, [], body)
        if inferred != value_type:
            raise SystemExit(
                f"checked definition {label} inferred {inferred!r}, "
                f"not {value_type!r}"
            )
        type_goal = poly.mono.app(
            "MPolyType", poly.mono.nat(0), poly.encode_tp(value_type)
        )
        check_both(
            args.cetta, args.differential, dag_totals, type_goal,
            definitions.poly_type_article(0, value_type),
            accepted=True, native_status="ok",
        )
        check_both(
            args.cetta, args.differential, dag_totals,
            term_typing_goal(signature, [], body, value_type),
            body_article,
            accepted=True, native_status="ok",
        )
        checks += 2
        polymorphic += int(prefix_count > 0)
        signature.insert(0, (identifier, value_type))

    proposition: poly.Tp = ("prop",)
    local_context = [proposition]
    local_term: poly.Tm = ("typeLam", ("var", 0))
    local_type, local_article = poly.type_proof(
        [], 0, local_context, local_term
    )
    if local_type != ("all", proposition):
        raise SystemExit("type abstraction did not lift its local context")
    check_both(
        args.cetta, args.differential, dag_totals,
        term_typing_goal([], local_context, local_term, local_type),
        local_article,
        accepted=True, native_status="ok",
    )
    checks += 1

    forged_goal, forged_article = forged_context_shift()
    check_both(
        args.cetta, args.differential, dag_totals, forged_goal, forged_article,
        accepted=False, native_status="premise-mismatch",
    )
    checks += 1

    known: list[tuple[str, poly.Tm]] = []
    theorem_checks = 0
    revision_checks = 0
    for theorem in checked_theorems(checked.stdout):
        definitions_in_scope = all_definitions[:theorem[5]]
        label, declared, goal, article = compile_theorem(
            theorem, definitions_in_scope, known
        )
        check_both(
            args.cetta, args.differential, dag_totals, goal, article,
            accepted=True, native_status="ok",
        )
        theorem_checks += 1
        checks += 1

        if (
            revision_checks == 0 and known
            and "(KNOWN " in sx.render(theorem[4])
        ):
            declarations = retained_declarations(definitions_in_scope)
            prior_known = known[1:]
            wrong_goal = poly.mono.app(
                "MDefinitionProves",
                full_environment(declarations, prior_known),
                poly.mono.nat(0), poly.encode_type_context([]),
                poly.encode_proof_context([]), poly.encode_tm(declared),
            )
            check_both(
                args.cetta, args.differential, dag_totals, wrong_goal, article,
                accepted=False, native_status="final-mismatch",
            )
            revision_checks += 1
            checks += 1

        known.insert(0, (theorem[1], declared))

    if theorem_checks != 10 or revision_checks != 1:
        raise SystemExit(
            "incremental theorem admission did not exercise its full package"
        )
    if dag_totals.shared_checks == 0:
        raise SystemExit("the tactics package did not exercise proof-DAG sharing")
    if dag_totals.shared_bytes >= dag_totals.raw_bytes:
        raise SystemExit("shared physical articles did not reduce serialized size")

    print(
        "(NikMegalodonTacticsPackageV1Summary definitions=8 "
        f"polymorphic={polymorphic} definition-checks=16 "
        f"context-lift-checks=2 theorem-checks={theorem_checks} "
        f"revision-rejections={revision_checks} total-checks={checks} "
        f"dag-raw-nodes={dag_totals.raw_nodes} "
        f"dag-unique-nodes={dag_totals.unique_nodes} "
        f"dag-raw-patterns={dag_totals.raw_patterns} "
        f"dag-unique-patterns={dag_totals.unique_patterns} "
        f"raw-bytes={dag_totals.raw_bytes} "
        f"shared-bytes={dag_totals.shared_bytes} "
        f"dag-shared-checks={dag_totals.shared_checks} "
        "megalodon-checked=1 production-realizations=1 "
        "qualification-realizations=3)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
