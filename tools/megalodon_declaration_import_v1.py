#!/usr/bin/env python3
"""Compile Megalodon declaration exports to ordered NIK admission evidence.

The input is the existing Megalodon -sexprinfo stream, not a second parser for
its surface language. Every declaration retains its source identity and order.
The output is untrusted evidence checked by TheoryAdmissionKernel. Axioms are
assumptions, including library facts exported as AXIOM; none becomes a theorem.
Proof-bearing documents use the existing proof compiler and require matched
THM/PROOF/QED events. Declaration-only mode rejects them. Neither mode turns
an admitted or unfinished source proof into an axiom.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass, field, replace
from pathlib import Path
import subprocess

import gslt2parse_schema_v1 as sx
import nik_proof_dag_v1 as dag
import megalodon_definition_conversion_v1 as evidence
import test_nik_megalodon_polymorphic_v1 as poly
import megalodon_proof_compile_v1 as proofs
import megalodon_source_prefix_v1 as source_prefix

app = poly.mono.app
node = poly.mono.proof_node


def natural(value: sx.SExpr, field_name: str) -> int:
    if type(value) is not int or value < 0:
        raise ValueError(f"{field_name} must be a natural number")
    return value


def string(value: sx.SExpr, field_name: str) -> str:
    if not isinstance(value, sx.StringLiteral):
        raise ValueError(f"{field_name} must be a string")
    return value.text


@dataclass(frozen=True)
class Declaration:
    kind: str
    label: str
    identifier: str
    line: int
    type: poly.Tp | None = None
    body: poly.Tm | None = None
    index: int | None = None
    proof: sx.SExpr | None = None
    proof_identifier: str | None = None
    reported_known: tuple[str, ...] = ()
    reported_delta: tuple[str, ...] = ()


def declarations(output: str) -> list[Declaration]:
    result = []
    for form in sx.parse_sexprs(output, source="Megalodon -sexprinfo"):
        head, args = poly.tag(form, "Megalodon declaration")
        if head == "PRIM" and len(args) == 5:
            index, label, identifier, value_type, line = args
            result.append(Declaration(head, string(label, "label"),
                string(identifier, "identity"), natural(line, "line"),
                poly.parse_tp(value_type), index=natural(index, "primitive index")))
        elif head in {"PARAM", "DEF", "AXIOM"} and len(args) == (6 if head == "DEF" else 5):
            label, identifier, count, *payload = args
            prefix = natural(count, "type-quantifier count")
            value_type = None if head == "AXIOM" else poly.parse_tp(
                source_prefix.to_binders(payload[0], prefix))
            body = poly.parse_tm(source_prefix.to_binders(payload[-2], prefix)) if head != "PARAM" else None
            for _ in range(prefix):
                if value_type is not None:
                    value_type = ("all", value_type)
                if body is not None:
                    body = ("typeAll" if head == "AXIOM" else "typeLam", body)
            result.append(Declaration(head, string(label, "label"),
                string(identifier, "identity"), natural(payload[-1], "line"),
                value_type, body))
        else:
            raise ValueError(f"not a declaration-only source stream: {head}")
    return result


def document(output: str) -> list[Declaration]:
    """Retain source order, with a theorem published only by a matched QED.

    DELTA and USESKNOWN are retained source metadata, not trusted premises.
    The generated derivation independently checks every use and conversion.
    """
    result: list[Declaration] = []
    pending: Declaration | None = None
    pending_prefix = 0
    for form in sx.parse_sexprs(output, source="Megalodon proof document"):
        head, args = poly.tag(form, "Megalodon source event")
        if head == "THM" and len(args) == 6 and pending is None:
            label, identifier, proof_identifier, count, body, line = args
            pending_prefix = natural(count, "type-quantifier count")
            proposition = poly.parse_tm(source_prefix.to_binders(body, pending_prefix))
            for _ in range(pending_prefix):
                proposition = ("typeAll", proposition)
            pending = Declaration("THM", string(label, "label"),
                string(identifier, "identity"), natural(line, "line"),
                body=proposition, proof_identifier=string(proof_identifier, "proof identity"))
        elif head in {"DELTA", "USESKNOWN"} and len(args) == 1 and pending is not None:
            identity = string(args[0], "dependency identity")
            if head == "DELTA":
                pending = replace(pending, reported_delta=(*pending.reported_delta, identity))
            else:
                pending = replace(pending, reported_known=(*pending.reported_known, identity))
        elif head == "PROOF" and len(args) == 2 and pending is not None and pending.proof is None:
            if string(args[0], "proof label") != pending.label:
                raise ValueError("source proof label does not match its theorem")
            pending = replace(pending, proof=source_prefix.to_binders(args[1], pending_prefix))
        elif head == "QED" and not args and pending is not None and pending.proof is not None:
            result.append(pending)
            pending = None
        elif head in {"PRIM", "PARAM", "DEF", "AXIOM"} and pending is None:
            result.extend(declarations(sx.render(form)))
        else:
            raise ValueError(f"unfinished, unsupported or misplaced source event: {head}")
    if pending is not None:
        raise ValueError("source theorem has no matched proof and QED")
    return result


def egal_initial_primitives() -> list[poly.Tp]:
    """Megalodon syntax.ml reserves primitive 0 for polymorphic choice.

    Its primname does not export this slot. Eps_i in the source is a separate
    named parameter. This explicit initial signature is not an axiom inventory.
    """
    variable = ("var", 0)
    return [("all", ("arr", ("arr", variable, ("prop",)), variable))]


@dataclass
class State:
    primitives: list[poly.Tp] = field(default_factory=list)
    terms: list[evidence.TermDecl] = field(default_factory=list)
    known: list[tuple[str, poly.Tm]] = field(default_factory=list)

    def environment(self) -> sx.SExpr:
        return app("MFullEnvironment", poly.encode_primitives(self.primitives),
                   evidence.encode_declarations(self.terms), poly.encode_known(self.known))

    def signature(self) -> list[tuple[str, poly.Tp]]:
        return [(name, value_type) for name, value_type, _ in self.terms]

    def declaration_effect(self, declaration: Declaration) -> tuple[sx.SExpr, State]:
        """Describe the requested environment extension, without validating it.

        This is claim construction, not admission. In particular a well-shaped
        false theorem produces a claim here; only replay can establish it.
        Evidence generation and evidence reuse request this same exact effect.
        The original state is not mutated.
        """
        following = State(list(self.primitives), list(self.terms), list(self.known))
        identifier = app(declaration.identifier)
        kind, value_type, body = declaration.kind, declaration.type, declaration.body
        if kind == "PRIM" and value_type is not None:
            if declaration.index != len(self.primitives):
                raise ValueError(f"primitive {declaration.label} is not the next source slot")
            item = app("MTheoryPrimitive", identifier, poly.mono.nat(declaration.index),
                       poly.encode_tp(value_type))
            following.primitives.append(value_type)
            following.terms.insert(0, (declaration.identifier, value_type,
                                       ("prim", declaration.index)))
        elif kind == "PARAM" and value_type is not None:
            item = app("MTheoryParameter", identifier, poly.encode_tp(value_type))
            following.terms.insert(0, (declaration.identifier, value_type, None))
        elif kind == "DEF" and value_type is not None and body is not None:
            item = app("MTheoryDefinition", identifier, poly.encode_tp(value_type),
                       poly.encode_tm(body))
            following.terms.insert(0, (declaration.identifier, value_type, body))
        elif (kind == "AXIOM" or (kind == "THM" and declaration.proof is not None)) and body is not None:
            item = app("MTheoryAxiom" if kind == "AXIOM" else "MTheoryTheorem",
                       identifier, poly.encode_tm(body))
            following.known.insert(0, (declaration.identifier, body))
        else:
            raise ValueError(f"malformed declaration {declaration.label}")
        return item, following

    def formation(self, proposition: poly.Tm, depth: int = 0) -> sx.SExpr:
        signature = poly.encode_signature(self.signature())
        args = [poly.encode_primitives(self.primitives),
                evidence.encode_declarations(self.terms), signature,
                poly.mono.nat(depth), poly.encode_type_context([])]
        if proposition[0] == "typeAll":
            body = proposition[1]
            return node("megalodon-theory-proposition-type-all",
                        [*args, poly.encode_tm(body), poly.encode_tm(body)],
                        [self.formation(body, depth + 1)])
        inferred, typing = poly.type_proof(self.signature(), depth, [], proposition,
                                          self.primitives)
        if inferred != ("prop",):
            raise ValueError("axiom body is not a proposition")
        return node("megalodon-theory-proposition-plain",
                    [*args, poly.encode_tm(proposition), poly.encode_tm(proposition)],
                    [evidence.project_signature(self.terms), typing,
                     evidence.path_refl(args[1], proposition)])

    def admit(self, declaration: Declaration) -> tuple[sx.SExpr, sx.SExpr]:
        """Compute one exact transition; mutate state only after constructing it."""
        before, item, after, proof = self._admission_transition(declaration)
        return app("MMegalodonTheoryAdmits", before, item, after), proof

    def _admission_transition(self, declaration: Declaration):
        before = self.environment()
        item, following = self.declaration_effect(declaration)
        primitives = poly.encode_primitives(self.primitives)
        terms = evidence.encode_declarations(self.terms)
        known = poly.encode_known(self.known)
        signature = poly.encode_signature(self.signature())
        identifier = app(declaration.identifier)
        kind, value_type, body = declaration.kind, declaration.type, declaration.body
        if kind == "PRIM":
            encoded_type = poly.encode_tp(value_type)
            extension = node("megalodon-theory-primitive-append-zero", [encoded_type], [])
            tail: list[poly.Tp] = []
            for head in reversed(self.primitives):
                extension = node("megalodon-theory-primitive-append-succ",
                    [poly.encode_tp(head), poly.encode_primitives(tail),
                     poly.mono.nat(len(tail)), encoded_type,
                     poly.encode_primitives([*tail, value_type])], [extension])
                tail = [head, *tail]
            proof = node("megalodon-theory-admit-primitive",
                [primitives, terms, known, identifier, poly.mono.nat(declaration.index),
                 encoded_type, poly.encode_primitives(following.primitives)],
                [evidence.poly_type_article(0, value_type), extension])
        elif kind == "PARAM" and value_type is not None:
            proof = node("megalodon-theory-admit-parameter",
                [primitives, terms, known, identifier, poly.encode_tp(value_type)],
                [evidence.poly_type_article(0, value_type)])
        elif kind == "DEF" and value_type is not None and body is not None:
            inferred, typing = poly.type_proof(self.signature(), 0, [], body, self.primitives)
            if inferred != value_type:
                raise ValueError(f"definition {declaration.label} has the wrong type")
            proof = node("megalodon-theory-admit-definition",
                [primitives, terms, signature, known, identifier,
                 poly.encode_tp(value_type), poly.encode_tm(body)],
                [evidence.project_signature(self.terms),
                 evidence.poly_type_article(0, value_type), typing])
        elif kind == "AXIOM" and body is not None:
            proof = node("megalodon-theory-admit-axiom",
                [primitives, terms, signature, known, identifier, poly.encode_tm(body)],
                [self.formation(body)])
        elif kind == "THM" and body is not None and declaration.proof is not None:
            _, proof_article = proofs.compile_proposition(
                self.terms, self.known, body, declaration.proof, self.primitives)
            proof = node("megalodon-theory-admit-theorem",
                [primitives, terms, signature, known, identifier, poly.encode_tm(body)],
                [self.formation(body), proof_article])
        else:
            raise ValueError(f"malformed declaration {declaration.label}")
        self.primitives, self.terms, self.known = following.primitives, following.terms, following.known
        return before, item, self.environment(), proof


def document_claim(state: State, items: list[Declaration]) -> sx.SExpr:
    """Request the exact ordered prefix, independently of its proof construction.

    Neither this function nor equality with a stored claim grants acceptance.
    A retained article must be replayed against the returned goal. Source order,
    definition bodies, types, primitive slots and axiom/theorem distinctions are
    present in the goal. Labels and source line numbers are not logical premises.
    """
    initial = state.environment()
    encoded_items = []
    for declaration in items:
        item, state = state.declaration_effect(declaration)
        encoded_items.append(item)
    remaining = app("MTheoryItemsNil")
    for item in reversed(encoded_items):
        remaining = app("MTheoryItemsCons", item, remaining)
    return app("MMegalodonTheoryChecks", initial, remaining, state.environment())


def compile_document(state: State, items: list[Declaration]) -> tuple[sx.SExpr, sx.SExpr]:
    """Compile the entire ordered document to the existing theory-checks rule.

    Every adjacent transition must meet at the same environment in the NIK
    derivation itself; Python iteration order is not an acceptance premise.
    """
    initial = state.environment()
    transitions = [state._admission_transition(item) for item in items]
    final = state.environment()
    remaining = app("MTheoryItemsNil")
    proof = node("megalodon-theory-checks-nil", [final], [])
    for before, item, after, admission in reversed(transitions):
        proof = node("megalodon-theory-checks-cons",
            [before, item, remaining, after, final], [admission, proof])
        remaining = app("MTheoryItemsCons", item, remaining)
    return app("MMegalodonTheoryChecks", initial, remaining, final), proof


def export_document(megalodon: Path, source: Path,
                    signatures: list[Path] | tuple[Path, ...] = ()) -> str:
    """Export the complete ordered signature prefix and main proof document.

    Megalodon's export flag precedes every include: importing a signature must
    not hide its assumptions or definitions from the subsequent NIK replay.
    A failed source check produces no usable document.
    """
    command = [str(megalodon.resolve()), "-sexprinfo"]
    for signature in signatures:
        command.extend(["-I", str(signature.resolve())])
    command.append(str(source.resolve()))
    return subprocess.run(command, capture_output=True, text=True, check=True).stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--include", type=Path, action="append", default=[],
                        help="source signature to import and replay in order; repeat for several")
    parser.add_argument("--profile", choices=["empty", "egal"], required=True)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--with-proofs", action="store_true",
                        help="require and compile retained proofs for THM/PROOF/QED events")
    args = parser.parse_args()
    exported = export_document(args.megalodon, args.source, args.include)
    items = (document if args.with_proofs else declarations)(exported)
    state = State(primitives=egal_initial_primitives() if args.profile == "egal" else [])
    counts: Counter[str] = Counter()
    for ordinal, declaration in enumerate(items, 1):
        goal, proof = state.admit(declaration)
        article = dag.compile_shared_article(goal, proof).article
        poly.require_public_result(poly.run_cetta(args.cetta, goal, article), accepted=True)
        counts[declaration.kind] += 1
        print(f"{ordinal}/{len(items)} {declaration.kind} {declaration.label}: checked", flush=True)
    print("MegalodonDeclarationImportV1", dict(counts))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
