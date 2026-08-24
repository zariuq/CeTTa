#!/usr/bin/env python3
"""Lower pinned Nil chaining sources into finite-Horn rule-machine guests."""

from __future__ import annotations

from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path
import argparse
import sys

import gslt2parse_schema_v1 as sx


PINNED_SHA256 = {
    "curried": "e2b3c1132bede217773bcad0cff25f7ccde2b137a7dbca135cd2512f6e109386",
    "bfc": "b53b6079a03241f39fe7d8750b77247ce959a73c95dc55cc2b419d83df3ce5b1",
    "synthesis": "ba3279cfbdd737c67b4118cd734c0f69e0dfcb87c0b4035a082c0d658d78ac3e",
    "sumo_kb": "a8df9448de199882f944e1010de42d5641a5ccf8829d1c6963fcd262f85007db",
    "sumo_rules": "9e6f4df984188e023400af78227132178996cfd0f38c4a8f3398fc085719ab55",
}


class GuestError(RuntimeError):
    pass


def metta_string_compatible_text(text: str) -> str:
    """Make MeTTa's multiline strings acceptable to the JSON-string parser."""
    output: list[str] = []
    quoted = False
    escaped = False
    for char in text:
        if quoted:
            if escaped:
                output.append(char)
                escaped = False
            elif char == "\\":
                output.append(char)
                escaped = True
            elif char == '"':
                output.append(char)
                quoted = False
            elif char == "\n":
                output.append("\\n")
            elif char == "\r":
                output.append("\\r")
            else:
                output.append(char)
        else:
            output.append(char)
            if char == '"':
                quoted = True
    return "".join(output)


def parse_source(text: str, source: str) -> list[sx.SExpr]:
    return sx.parse_sexprs(metta_string_compatible_text(text), source=source)


def pinned_text(path: Path, role: str) -> str:
    data = path.read_bytes()
    actual = sha256(data).hexdigest()
    expected = PINNED_SHA256[role]
    if actual != expected:
        raise GuestError(
            f"{role} source identity mismatch: expected {expected}, got {actual}"
        )
    return data.decode("utf-8")


def symbol(term: sx.SExpr, context: str) -> str:
    if not isinstance(term, sx.Symbol):
        raise GuestError(f"{context}: expected a symbol, got {sx.render(term)}")
    return term.text


def as_tuple(term: sx.SExpr, context: str) -> tuple[sx.SExpr, ...]:
    if not isinstance(term, tuple):
        raise GuestError(f"{context}: expected a list, got {sx.render(term)}")
    return term


def gslt_term(term: sx.SExpr) -> sx.SExpr:
    if isinstance(term, sx.Symbol) and term.text.startswith("$"):
        name = term.text[1:]
        if not name or name == "_":
            raise GuestError(f"unsupported anonymous source variable {term.text!r}")
        return sx.Variable(name)
    if isinstance(term, tuple):
        return tuple(gslt_term(item) for item in term)
    return term


def cons(items: list[sx.SExpr]) -> sx.SExpr:
    result: sx.SExpr = sx.Symbol("rm-nil")
    for item in reversed(items):
        result = (sx.Symbol("rm-cons"), item, result)
    return result


def premise_list(proofs: list[sx.Variable], goals: list[sx.SExpr]) -> sx.SExpr:
    return cons(
        [(sx.Symbol("rm-premise"), proof, goal) for proof, goal in zip(proofs, goals)]
    )


def rule_form(name: str, head: sx.SExpr, body: list[sx.SExpr]) -> sx.SExpr:
    return (
        sx.Symbol("rule"),
        sx.Symbol(name),
        (sx.Symbol("head"), head),
        (sx.Symbol("body"), *body),
    )


@dataclass(frozen=True)
class Block:
    identifier: str
    source: str
    proof: sx.SExpr
    premises: sx.SExpr
    conclusion: sx.SExpr

    def term(self) -> sx.SExpr:
        return (
            sx.Symbol("rm-block"),
            sx.Symbol(self.identifier),
            sx.Symbol(self.source),
            self.proof,
            self.premises,
            self.conclusion,
        )


def declarations_from_rule_base(text: str, source_name: str) -> list[Block]:
    forms = parse_source(text, source_name)
    blocks: list[Block] = []
    for index, raw in enumerate(forms):
        form = as_tuple(raw, f"{source_name}: form {index}")
        if len(form) != 3 or symbol(form[0], f"{source_name}: form {index}") != ":":
            raise GuestError(f"{source_name}: expected only type declarations")
        constructor = symbol(form[1], f"{source_name}: constructor")
        arrow = as_tuple(form[2], f"{source_name}: arrow")
        if len(arrow) < 2 or symbol(arrow[0], f"{source_name}: arrow head") != "->":
            raise GuestError(f"{source_name}: malformed rule declaration {constructor}")
        arguments = [gslt_term(item) for item in arrow[1:]]
        goals, conclusion = arguments[:-1], arguments[-1]
        proof_variables = [sx.Variable(f"proof{slot}") for slot in range(len(goals))]
        blocks.append(
            Block(
                identifier=f"rule-{index:03d}-{constructor}",
                source=f"{source_name}:form-{index:03d}",
                proof=(sx.Symbol("rm-proof-app"), sx.Symbol(constructor), cons(proof_variables)),
                premises=premise_list(proof_variables, goals),
                conclusion=conclusion,
            )
        )
    return blocks


def require_form(forms: list[sx.SExpr], text: str, source: str, label: str) -> None:
    wanted = one_form(text, f"expected {label}")
    if wanted not in forms:
        raise GuestError(f"{source}: missing semantic witness {label}")


def curried_chaining_blocks(text: str) -> list[Block]:
    """Extract the authored facts and curried Modus Ponens rule."""
    source = "curried-chainer.metta"
    forms = parse_source(text, source)
    declarations: dict[str, sx.SExpr] = {}
    for raw in forms:
        if not isinstance(raw, tuple) or len(raw) != 3:
            continue
        if not isinstance(raw[0], sx.Symbol) or raw[0].text != "add-atom":
            continue
        if not isinstance(raw[1], sx.Symbol) or raw[1].text != "&kb":
            continue
        declaration = as_tuple(raw[2], f"{source}: add-atom declaration")
        if len(declaration) != 3 or symbol(declaration[0], source) != ":":
            raise GuestError(f"{source}: malformed add-atom declaration")
        name = symbol(declaration[1], f"{source}: declaration name")
        if name in declarations:
            raise GuestError(f"{source}: duplicate declaration {name}")
        declarations[name] = declaration[2]

    expected = {"a", "ab", "bc", "ModusPonens"}
    if set(declarations) != expected:
        raise GuestError(
            f"{source}: expected declarations {sorted(expected)}, "
            f"got {sorted(declarations)}"
        )
    require_form(
        forms,
        "(assertEqual (bc &kb (fromNumber 3) (: $prf C)) "
        "(: ((ModusPonens bc) ((ModusPonens ab) a)) C))",
        source,
        "depth-three backward proof of C",
    )
    require_form(
        forms,
        "(assertEqualToResult (fc &kb (fromNumber 3) (: a A)) "
        "((: a A) (: ((ModusPonens ab) a) B) "
        "(: ((ModusPonens bc) ((ModusPonens ab) a)) C)))",
        source,
        "depth-three forward occurrences from a",
    )

    mp_type = as_tuple(declarations["ModusPonens"], f"{source}: ModusPonens")
    expected_mp = one_form(
        "(-> (→ $p $q) (-> $p $q))", "expected curried Modus Ponens type"
    )
    if mp_type != expected_mp:
        raise GuestError(f"{source}: ModusPonens declaration changed")
    if declarations["a"] != sx.Symbol("A"):
        raise GuestError(f"{source}: axiom a changed")
    expected_ab = one_form("(→ A B)", "expected ab theorem")
    expected_bc = one_form("(→ B C)", "expected bc theorem")
    if declarations["ab"] != expected_ab or declarations["bc"] != expected_bc:
        raise GuestError(f"{source}: implication facts changed")

    p = sx.Variable("p")
    q = sx.Variable("q")
    implication_proof = sx.Variable("implicationProof")
    premise_proof = sx.Variable("premiseProof")
    implication = sx.Symbol("→")
    return [
        Block(
            "a",
            f"{source}:a",
            (sx.Symbol("rm-proof-atom"), sx.Symbol("a")),
            sx.Symbol("rm-nil"),
            sx.Symbol("A"),
        ),
        Block(
            "ab",
            f"{source}:ab",
            (sx.Symbol("rm-proof-atom"), sx.Symbol("ab")),
            sx.Symbol("rm-nil"),
            expected_ab,
        ),
        Block(
            "bc",
            f"{source}:bc",
            (sx.Symbol("rm-proof-atom"), sx.Symbol("bc")),
            sx.Symbol("rm-nil"),
            expected_bc,
        ),
        Block(
            "modus-ponens",
            f"{source}:ModusPonens",
            (
                sx.Symbol("rm-proof-app"),
                sx.Symbol("ModusPonens"),
                cons([implication_proof, premise_proof]),
            ),
            premise_list(
                [implication_proof, premise_proof],
                [(implication, p, q), p],
            ),
            q,
        ),
    ]


def sumo_blocks(kb_text: str, rules_text: str) -> list[Block]:
    facts: list[Block] = []
    for index, raw in enumerate(parse_source(kb_text, "john-carry-flower.kif.metta")):
        conclusion = gslt_term(raw)
        facts.append(
            Block(
                identifier=f"fact-{index:03d}",
                source=f"john-carry-flower.kif.metta:form-{index:03d}",
                proof=(
                    sx.Symbol("rm-proof-atom"),
                    (sx.Symbol("WitnessOf"), conclusion),
                ),
                premises=sx.Symbol("rm-nil"),
                conclusion=conclusion,
            )
        )
    return facts + declarations_from_rule_base(rules_text, "rule-base.metta")


def contains_term(term: sx.SExpr, wanted: sx.SExpr) -> bool:
    if term == wanted:
        return True
    return isinstance(term, tuple) and any(contains_term(child, wanted) for child in term)


def one_form(text: str, source: str) -> sx.SExpr:
    forms = parse_source(text, source)
    if len(forms) != 1:
        raise GuestError(f"{source}: expected one witness form")
    return forms[0]


def require_embedded_term(forms: list[sx.SExpr], text: str, label: str) -> None:
    wanted = one_form(text, f"expected {label}")
    if not any(contains_term(form, wanted) for form in forms):
        raise GuestError(f"bfc-xp.mm2: missing semantic witness {label}")


def bfc_blocks(text: str) -> list[Block]:
    """Extract the four proof rules embedded in Nil's MM2 BFC executor."""
    forms = parse_source(text, "bfc-xp.mm2")
    require_embedded_term(forms, "(→ $𝜑 (→ $𝜓 $𝜑))", "ax1")
    require_embedded_term(
        forms,
        "(→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))",
        "ax2",
    )
    require_embedded_term(
        forms, "(→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))", "ax3"
    )
    if not any(contains_term(form, sx.Symbol("mpⁱ")) for form in forms):
        raise GuestError("bfc-xp.mm2: missing inverse-MP proof constructor")
    require_embedded_term(
        forms,
        "(→ (→ (→ 𝜑 𝜓) 𝜒) (→ 𝜓 𝜒))",
        "jarr target",
    )

    p = sx.Variable("p")
    q = sx.Variable("q")
    r = sx.Variable("r")
    antecedent = sx.Variable("antecedent")
    conclusion = sx.Variable("conclusion")
    left = sx.Variable("left")
    right = sx.Variable("right")
    imp = sx.Symbol("imp")
    neg = sx.Symbol("neg")
    return [
        Block(
            "ax1",
            "bfc-xp.mm2:ax1",
            (sx.Symbol("rm-proof-atom"), sx.Symbol("ax1")),
            sx.Symbol("rm-nil"),
            (imp, p, (imp, q, p)),
        ),
        Block(
            "ax2",
            "bfc-xp.mm2:ax2",
            (sx.Symbol("rm-proof-atom"), sx.Symbol("ax2")),
            sx.Symbol("rm-nil"),
            (imp, (imp, p, (imp, q, r)), (imp, (imp, p, q), (imp, p, r))),
        ),
        Block(
            "ax3",
            "bfc-xp.mm2:ax3",
            (sx.Symbol("rm-proof-atom"), sx.Symbol("ax3")),
            sx.Symbol("rm-nil"),
            (imp, (imp, (neg, p), (neg, q)), (imp, q, p)),
        ),
        Block(
            "mpi",
            "bfc-xp.mm2:mpi",
            (sx.Symbol("rm-proof-app"), sx.Symbol("mpi"), cons([left, right])),
            premise_list(
                [left, right],
                [antecedent, (imp, antecedent, conclusion)],
            ),
            conclusion,
        ),
    ]


def synth_type(term: sx.SExpr) -> sx.SExpr:
    if isinstance(term, tuple):
        if not term:
            return term
        head = term[0]
        mapped_head = (
            sx.Symbol("arrow")
            if isinstance(head, sx.Symbol) and head.text == "->"
            else synth_type(head)
        )
        return (mapped_head, *(synth_type(child) for child in term[1:]))
    return gslt_term(term)


def synthesis_declarations(text: str) -> tuple[list[Block], Block]:
    forms = parse_source(text, "SynthesizeTest.metta")
    declarations: dict[str, tuple[sx.SExpr, ...]] = {}
    for raw in forms:
        if not isinstance(raw, tuple) or len(raw) != 3:
            continue
        if not isinstance(raw[0], sx.Symbol) or raw[0].text != "=":
            continue
        lhs, rhs = raw[1], raw[2]
        if not isinstance(lhs, tuple) or len(lhs) != 1 or not isinstance(lhs[0], sx.Symbol):
            continue
        if (
            lhs[0].text not in {"kb", "rb"}
            or not isinstance(rhs, tuple)
            or len(rhs) != 2
            or not isinstance(rhs[0], sx.Symbol)
            or rhs[0].text != "superpose"
            or not isinstance(rhs[1], tuple)
        ):
            continue
        declarations[lhs[0].text] = rhs[1]
    if set(declarations) != {"kb", "rb"}:
        raise GuestError("SynthesizeTest.metta: could not locate kb/rb declarations")

    facts: list[Block] = []
    for index, declaration in enumerate(declarations["kb"]):
        form = as_tuple(declaration, "SynthesizeTest.metta: kb declaration")
        if len(form) != 3 or symbol(form[0], "kb declaration") != ":":
            raise GuestError("SynthesizeTest.metta: malformed kb declaration")
        name = symbol(form[1], "kb function")
        facts.append(
            Block(
                name,
                f"SynthesizeTest.metta:{name}",
                (sx.Symbol("rm-proof-atom"), sx.Symbol(name)),
                sx.Symbol("rm-nil"),
                synth_type(form[2]),
            )
        )

    rules: list[Block] = []
    rule_names = {".": "compose", ".:": "blackbird"}
    for declaration in declarations["rb"]:
        form = as_tuple(declaration, "SynthesizeTest.metta: rb declaration")
        if len(form) != 3 or symbol(form[0], "rb declaration") != ":":
            raise GuestError("SynthesizeTest.metta: malformed rb declaration")
        source_name = symbol(form[1], "rb function")
        identifier = rule_names.get(source_name)
        if not identifier:
            raise GuestError(f"SynthesizeTest.metta: unsupported rule {source_name}")
        arrow = as_tuple(form[2], f"SynthesizeTest.metta: {source_name} type")
        if len(arrow) < 2 or symbol(arrow[0], "rb arrow") != "->":
            raise GuestError("SynthesizeTest.metta: malformed rb arrow")
        arguments = [synth_type(item) for item in arrow[1:]]
        proof_vars = [sx.Variable(f"{identifier}Proof{i}") for i in range(len(arguments) - 1)]
        rules.append(
            Block(
                identifier,
                f"SynthesizeTest.metta:{identifier}",
                (sx.Symbol("rm-proof-app"), sx.Symbol(identifier), cons(proof_vars)),
                premise_list(proof_vars, arguments[:-1]),
                arguments[-1],
            )
        )
    by_id = {block.identifier: block for block in rules}
    if set(by_id) != {"compose", "blackbird"}:
        raise GuestError("SynthesizeTest.metta: expected composition and Blackbird")
    return facts + [by_id["compose"]], by_id["blackbird"]


CORE_OPERATORS = {
    ("artifact-empty", 1),
    ("artifact-link", 2),
    ("block-ref", 2),
    ("guest-revision", 3),
    ("rm-block", 5),
    ("rm-cons", 2),
    ("rm-premise", 2),
    ("rm-proof-app", 2),
    ("rm-proof-atom", 1),
    ("source-block-id", 3),
    ("source-block", 2),
}
SCHEMA_WRAPPERS = {"rule", "head", "body"}


def operators_in(term: sx.SExpr, output: set[tuple[str, int]]) -> None:
    if not isinstance(term, tuple):
        return
    if (
        term
        and isinstance(term[0], sx.Symbol)
        and term[0].text not in SCHEMA_WRAPPERS
        and len(term) > 1
    ):
        output.add((term[0].text, len(term) - 1))
    for child in term:
        operators_in(child, output)


def artifact_term(guest: str, blocks: list[Block]) -> sx.SExpr:
    artifact: sx.SExpr = (sx.Symbol("artifact-empty"), sx.Symbol(guest))
    for block in blocks:
        artifact = (
            sx.Symbol("artifact-link"),
            artifact,
            (sx.Symbol("block-ref"), sx.Symbol(guest), sx.Symbol(block.identifier)),
        )
    return artifact


def render_guest_presentation(
    blocks: list[Block], *, guest: str, presentation: str, rule_prefix: str,
    provenance: str,
) -> str:
    rules: list[sx.SExpr] = []
    for block in blocks:
        rules.append(
            rule_form(
                f"{rule_prefix}-{block.identifier}",
                (sx.Symbol("source-block"), sx.Symbol(guest), block.term()),
                [],
            )
        )
    rules.append(
        rule_form(
            f"{rule_prefix}-r0",
            (
                sx.Symbol("guest-revision"),
                sx.Symbol(guest),
                sx.Symbol("r0"),
                artifact_term(guest, blocks),
            ),
            [],
        )
    )

    operators: set[tuple[str, int]] = set()
    for rule in rules:
        operators_in(rule, operators)
    operators.difference_update(CORE_OPERATORS)
    signature = "\n".join(
        f"    (operator {name} {arity})" for name, arity in sorted(operators)
    )
    rewrites = "\n".join(f"    {sx.render(rule)}" for rule in rules)
    return (
        f"; Generated from {provenance}.\n"
        f"(gslt-presentation-v1 {presentation}\n"
        "  (signature\n"
        f"{signature})\n"
        "  (equations)\n"
        "  (rewrites\n"
        f"{rewrites}))\n"
    )


def render_sumo_presentation(blocks: list[Block]) -> str:
    return render_guest_presentation(
        blocks,
        guest="nil-sumo-john-carry-flower",
        presentation="NilSUMOJohnCarryFlowerV1",
        rule_prefix="nil-sumo",
        provenance="identity-pinned Nil/SUMO sources",
    )


def render_curried_presentation(blocks: list[Block]) -> str:
    return render_guest_presentation(
        blocks,
        guest="curried-chaining",
        presentation="CurriedChainingRulePackageV1",
        rule_prefix="curried-chaining",
        provenance="the identity-pinned curried chaining source",
    )


def runtime_term(term: sx.SExpr) -> sx.SExpr:
    if isinstance(term, sx.Variable):
        return sx.Symbol(f"${term.name}")
    if isinstance(term, tuple):
        return tuple(runtime_term(child) for child in term)
    return term


def equation(name: str, value: sx.SExpr) -> str:
    return sx.render(
        (
            sx.Symbol("="),
            (sx.Symbol(name),),
            runtime_term(value),
        )
    )


def package(blocks: list[Block]) -> sx.SExpr:
    return (sx.Symbol("rm-package"), *(block.term() for block in blocks))


def render_runtime_fixture(
    curried: list[Block], bfc: list[Block], synthesis_r0: list[Block],
    blackbird: Block, sumo: list[Block],
) -> str:
    definitions = [
        equation("chain-curried-package", package(curried)),
        equation("nil-bfc-package", package(bfc)),
        equation("nil-synthesis-r0-package", package(synthesis_r0)),
        equation("nil-synthesis-blackbird-block", blackbird.term()),
        equation("nil-sumo-package", package(sumo)),
        "(= (chain-curried-artifact) (compile:rule-package chain-curried-r0 (chain-curried-package)))",
        "(= (nil-bfc-special-block) (rm-block special-theorem runtime:add-special (rm-proof-atom special-proof) rm-nil special-theorem))",
        "(= (nil-bfc-artifact) (compile:rule-package bfc-r0 (nil-bfc-package)))",
        "(= (nil-bfc-rule-program) (compile:rule-program (nil-bfc-artifact)))",
        "(= (nil-bfc-r1-artifact) (compile:link-rule (nil-bfc-artifact) bfc-r1 (nil-bfc-special-block)))",
        "(= (nil-bfc-r1-rule-program) (compile:rule-program-link (nil-bfc-rule-program) bfc-r1 (nil-bfc-special-block)))",
        "(= (nil-synthesis-r0-artifact) (compile:rule-package synth-r0 (nil-synthesis-r0-package)))",
        "(= (nil-synthesis-r1-artifact) (compile:link-rule (nil-synthesis-r0-artifact) synth-r1 (nil-synthesis-blackbird-block)))",
        "(= (nil-sumo-artifact) (compile:rule-package sumo-r0 (nil-sumo-package)))",
    ]
    queries = [
        "!(compile:artifact-info (chain-curried-artifact))",
        "!(compile:run (chain-curried-artifact) 0 1000 10 A)",
        "!(compile:run (chain-curried-artifact) 2 1000 10 B)",
        "!(compile:run (chain-curried-artifact) 3 1000 10 C)",
        "!(compile:run (chain-curried-artifact) 3 1000 10 D)",
        "!(compile:rule-program (chain-curried-artifact))",
        "!(compile:artifact-info (nil-bfc-artifact))",
        "!(compile:run (nil-bfc-artifact) 0 1000 100 (imp a (imp b a)))",
        "!(compile:rule-program-info (nil-bfc-rule-program))",
        "!(compile:rule-program-run (nil-bfc-rule-program) 13 1000000 10 (imp (imp (imp p q) r) (imp q r)))",
        "!(compile:rule-program-run-native (nil-bfc-rule-program) 13 1000000 10 (imp (imp (imp p q) r) (imp q r)))",
        "!(compile:rule-program-info (nil-bfc-r1-rule-program))",
        "!(compile:rule-program-run (nil-bfc-rule-program) 1 1000 10 special-theorem)",
        "!(compile:rule-program-run (nil-bfc-r1-rule-program) 1 1000 10 special-theorem)",
        "!(compile:rule-program-run (nil-bfc-rule-program) 1 1000 10 special-theorem)",
        "!(compile:artifact-info (nil-synthesis-r0-artifact))",
        "!(compile:artifact-info (nil-synthesis-r1-artifact))",
        "!(compile:run (nil-synthesis-r0-artifact) 2 10000 100 (arrow String Number Number))",
        "!(compile:rule-program (nil-synthesis-r0-artifact))",
        "!(compile:run (nil-synthesis-r1-artifact) 2 10000 100 (arrow String Number Number))",
        "!(compile:run (nil-synthesis-r0-artifact) 2 10000 100 (arrow String Number Number))",
        "!(compile:artifact-info (nil-sumo-artifact))",
        "!(compile:run (nil-sumo-artifact) 3 1000000 100 (objectTransferred JohnsCarry JohnsFlower))",
        "!(compile:run (nil-sumo-artifact) 4 1000000 100 (objectTransferred JohnsCarry JohnsFlower))",
    ]
    return (
        "; Generated from identity-pinned Nil chaining sources.\n"
        + "\n".join(definitions)
        + "\n\n"
        + "\n".join(queries)
        + "\n"
    )

def write_if_changed(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    path.write_text(text, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--curried", type=Path, required=True)
    parser.add_argument("--bfc", type=Path, required=True)
    parser.add_argument("--synthesis", type=Path, required=True)
    parser.add_argument("--sumo-kb", type=Path, required=True)
    parser.add_argument("--sumo-rules", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--curried-out", type=Path, required=True)
    parser.add_argument("--runtime-out", type=Path)
    args = parser.parse_args()

    curried_text = pinned_text(args.curried, "curried")
    bfc_text = pinned_text(args.bfc, "bfc")
    synthesis_text = pinned_text(args.synthesis, "synthesis")
    kb_text = pinned_text(args.sumo_kb, "sumo_kb")
    rules_text = pinned_text(args.sumo_rules, "sumo_rules")
    curried = curried_chaining_blocks(curried_text)
    bfc = bfc_blocks(bfc_text)
    synthesis_r0, blackbird = synthesis_declarations(synthesis_text)
    sumo = sumo_blocks(kb_text, rules_text)
    write_if_changed(args.curried_out, render_curried_presentation(curried))
    write_if_changed(args.out, render_sumo_presentation(sumo))
    if args.runtime_out:
        write_if_changed(
            args.runtime_out,
            render_runtime_fixture(curried, bfc, synthesis_r0, blackbird, sumo),
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GuestError, sx.SchemaError, UnicodeError, OSError) as error:
        print(f"generate_nil_rule_guests_v1: {error}", file=sys.stderr)
        raise SystemExit(2)
