#!/usr/bin/env python3
"""Whole-evaluator discriminators for Prime bare-``$`` contenders.

The public fresh reader and alternate literal/shared readers are compared
through their complete evaluators.  This checks that their identity partitions
survive parsing, rewriting, evaluation, quotation, typing interfaces, result
projection, and grounded references.
"""

from __future__ import annotations

import argparse
import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Contender:
    name: str
    binary: Path


def run(
    contender: Contender,
    expressions: tuple[str, ...],
    *,
    language: str = "prime",
    pretty_vars: bool = False,
) -> subprocess.CompletedProcess[str]:
    command = [str(contender.binary), "--lang", language]
    if language == "he":
        command.extend(("--profile", "he-compat"))
    if pretty_vars:
        command.append("--pretty-vars")
    for expression in expressions:
        command.extend(("-e", expression))
    return subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def invoke(
    contender: Contender,
    expressions: tuple[str, ...],
    *,
    language: str = "prime",
    pretty_vars: bool = False,
) -> str:
    completed = run(
        contender,
        expressions,
        language=language,
        pretty_vars=pretty_vars,
    )
    if completed.returncode != 0:
        raise AssertionError(
            f"{contender.name}: evaluator exited {completed.returncode}:\n"
            f"{completed.stdout}"
        )
    return completed.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--literal", required=True, type=Path)
    parser.add_argument("--fresh", required=True, type=Path)
    parser.add_argument("--shared", required=True, type=Path)
    args = parser.parse_args()

    contenders = {
        "literal": Contender("literal", args.literal.resolve()),
        "fresh": Contender("fresh", args.fresh.resolve()),
        "shared": Contender("shared", args.shared.resolve()),
    }
    checks = 0

    def expect(
        policy: str,
        label: str,
        expressions: tuple[str, ...],
        expected: str,
        *,
        language: str = "prime",
        pretty_vars: bool = False,
    ) -> None:
        nonlocal checks
        actual = invoke(
            contenders[policy],
            expressions,
            language=language,
            pretty_vars=pretty_vars,
        )
        if actual != expected:
            raise AssertionError(
                f"{policy}/{label}: expected {expected!r}, got {actual!r}"
            )
        checks += 1

    def expect_syntax_rejection(
        policy: str,
        label: str,
        expressions: tuple[str, ...],
        *,
        language: str,
    ) -> None:
        nonlocal checks
        completed = run(
            contenders[policy], expressions, language=language
        )
        if completed.returncode != 1 or "could not parse" not in completed.stdout:
            raise AssertionError(
                f"{policy}/{label}: expected syntax rejection, got exit "
                f"{completed.returncode}:\n{completed.stdout}"
            )
        checks += 1

    # Matching power is independent of nameability: both variable contenders
    # match a grounded space reference, while the literal symbol does not.
    for policy, expected in {
        "literal": "[rejected]",
        "fresh": "[accepted]",
        "shared": "[accepted]",
    }.items():
        expect(
            policy,
            "grounded-reference",
            ("! (unify $ &self accepted rejected)",),
            expected,
        )

    # Raw equality compares syntax as supplied. A held space value is not
    # the unresolved spelling `&self`; nameability below compares two values.
    for policy in ("fresh", "shared"):
        expect(policy, "raw-space-spelling-is-not-a-held-reference",
            ("! (let $ &self (noreduce-eq $ &self))",), "[False]")

    # Two fresh anonymous variables are independent; one shared variable is an
    # equality constraint.  Equal fields expose the positive shared case.
    for policy, unequal, equal in (
        ("literal", "[rejected]", "[rejected]"),
        ("fresh", "[accepted]", "[accepted]"),
        ("shared", "[rejected]", "[accepted]"),
    ):
        expect(
            policy,
            "unequal-fields",
            ("! (unify (P $ $) (P a b) accepted rejected)",),
            unequal,
        )
        expect(
            policy,
            "equal-fields",
            ("! (unify (P $ $) (P a a) accepted rejected)",),
            equal,
        )

    # A discard pattern may accept a reference without making it nameable.
    # Reusing the same glyph in the body distinguishes fresh from shared.
    for policy, discarded, reused in (
        ("literal", "", ""),
        ("fresh", "[accepted]", "[False]"),
        ("shared", "[accepted]", "[True]"),
    ):
        expect(
            policy,
            "discard-reference",
            ("! (let $ &self accepted)",),
            discarded,
        )
        expect(
            policy,
            "body-referenceability",
            ("! (let $reference &self (let $ $reference (noreduce-eq $ $reference)))",),
            reused,
        )

    for policy, expected in (("fresh", "[False]"), ("shared", "[True]")):
        expect(
            policy,
            "chain-binder-referenceability",
            ("! (let $reference &self (chain $reference $ (noreduce-eq $ $reference)))",),
            expected,
        )
        expect(
            policy,
            "let-star-binder-referenceability",
            ("! (let $reference &self (let* (($ $reference)) (noreduce-eq $ $reference)))",),
            expected,
        )

    # Structured discard slots must remain independent under the fresh policy.
    for policy, expected in {
        "literal": "",
        "fresh": "[accepted]",
        "shared": "",
    }.items():
        expect(
            policy,
            "structured-discard",
            ("! (let (Pair $ $) (Pair a b) accepted)",),
            expected,
        )

    # Rewrite-rule occurrences consume the same identity partition produced by
    # the parser; this is not merely a direct-unifier behavior.
    for policy, unequal, equal in (
        ("literal", "[(f a b)]", "[(f a a)]"),
        ("fresh", "[accepted]", "[accepted]"),
        ("shared", "[(f a b)]", "[accepted]"),
    ):
        declaration = "(= (f $ $) accepted)"
        expect(
            policy,
            "rewrite-unequal-fields",
            (declaration, "! (f a b)"),
            unequal,
        )
        expect(
            policy,
            "rewrite-equal-fields",
            (declaration, "! (f a a)"),
            equal,
        )

    for policy in ("fresh", "shared"):
        expect(
            policy,
            "rewrite-grounded-reference",
            ("(= (accept-reference $) accepted)", "! (accept-reference &self)"),
            "[accepted]",
        )

    # Exact syntax equality observes identity; alpha-equivalence deliberately
    # quotients it.  Quotation prevents evaluation but preserves that syntax
    # distinction in the native reader.
    for policy, exact in {
        "literal": "[True]",
        "fresh": "[False]",
        "shared": "[True]",
    }.items():
        expect(
            policy,
            "exact-identity",
            ("! (noreduce-eq $ $)",),
            exact,
        )
        expect(
            policy,
            "quoted-exact-identity",
            ("! (noreduce-eq @$ @$)",),
            exact,
        )
        expect(
            policy,
            "quoted-alpha-equivalence",
            ("! (=alpha @$ @$)",),
            "[True]",
        )

    # Prime's sealed quotation retains the written variable under each
    # binder. `lift` is the explicit operation that puts a bound value into
    # code. HE's unsealed quotation remains a separate dialect contract.
    for policy in contenders:
        expect(
            policy,
            "quoted-named-let-binder",
            ("! (let $x a @$x)",),
            "[(quote $x)]",
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-named-let-star-binder",
            ("! (let* (($x a)) @$x)",),
            "[(quote $x)]",
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-named-lambda-binder",
            ("! ((lam $x @$x) a)",),
            "[(quote $x)]",
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-named-chain-binder",
            ("! (chain a $x @$x)",),
            "[(quote $x)]",
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-named-unifier-binding",
            ("! (unify $x a @$x no)",),
            "[(quote $x)]",
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-named-space-match-binding",
            (
                "! (let $s (new-space) "
                "(let $_ (add-atom $s (P a)) "
                "(match $s (P $x) @$x)))",
            ),
            "[(quote $x)]",
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-named-rewrite-binding",
            ("(= (quote-named $x) @$x)", "! (quote-named a)"),
            "[(quote $x)]",
            pretty_vars=True,
        )

        lifted_cases = (
            ("lifted-named-let-binder", ("! (let $x a (lift $x))",)),
            ("lifted-named-let-star-binder", ("! (let* (($x a)) (lift $x))",)),
            ("lifted-named-chain-binder", ("! (chain a $x (lift $x))",)),
            ("lifted-named-unifier-binding", ("! (unify $x a (lift $x) no)",)),
            ("lifted-named-space-match-binding", (
                "! (let $s (new-space) (let $_ (add-atom $s (P a)) (match $s (P $x) (lift $x))))",
            )),
            ("lifted-named-rewrite-binding", (
                "(= (lift-named $x) (lift $x))", "! (lift-named a)",
            )),
        )
        for label, expressions in lifted_cases:
            expect(policy, label, expressions, "[(quote a)]", pretty_vars=True)
        for label, expression in (
            ("he-quoted-named-let-binder", "! (let $x a (quote $x))"),
            ("he-quoted-named-chain-binder", "! (chain a $x (quote $x))"),
        ):
            expect(policy, label, (expression,), "[(quote a)]", language="he", pretty_vars=True)

    # Sealing retains both anonymous policies' written glyph. The exact
    # identity checks above distinguish their syntax identities separately.
    quote_rows = (
        (
            "fresh",
            "[(quote $)]",
            "[(quote $)]",
            "[(quote $)]",
            "[(quote $)]",
            "[(quote $)]",
            "[(quote $)]",
        ),
        (
            "shared",
            "[(quote $)]",
            "[(quote $)]",
            "[(quote $)]",
            "[(quote $)]",
            "[(quote $)]",
            "[(quote $)]",
        ),
    )
    for (
        policy,
        let_quote,
        let_star_quote,
        chain_quote,
        unify_quote,
        match_quote,
        rewrite_quote,
    ) in quote_rows:
        expect(
            policy,
            "quoted-let-binder",
            ("! (let $ a @$)",),
            let_quote,
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-let-star-binder",
            ("! (let* (($ a)) @$)",),
            let_star_quote,
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-chain-binder",
            ("! (chain a $ @$)",),
            chain_quote,
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-unifier-binding",
            ("! (unify $ a @$ no)",),
            unify_quote,
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-space-match-binding",
            (
                "! (let $s (new-space) "
                "(let $_ (add-atom $s (P a)) "
                "(match $s (P $) @$)))",
            ),
            match_quote,
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-rewrite-binding",
            ("(= (quote-bare $) @$)", "! (quote-bare a)"),
            rewrite_quote,
            pretty_vars=True,
        )

    # Running the held syntax in the retained enclosing context distinguishes
    # a fresh occurrence from the shared binder without changing its quotation.
    for policy, lexical, matcher in (
        ("fresh", "[$]", "[$]"),
        ("shared", "[a]", "[a]"),
    ):
        expect(
            policy,
            "unquoted-let-binder",
            ("! (let $ a (unquote @$))",),
            lexical,
            pretty_vars=True,
        )
        expect(
            policy,
            "unquoted-chain-binder",
            ("! (chain a $ (unquote @$))",),
            matcher,
            pretty_vars=True,
        )

    # Result and type-syntax projection must not collapse two fresh identities.
    for policy, pair_result, type_result in (
        ("literal", "[(Pair $ $)]", "[(-> $ $)]"),
        ("fresh", "[(Pair $ $_1)]", "[(-> $ $_1)]"),
        ("shared", "[(Pair $ $)]", "[(-> $ $)]"),
    ):
        expect(
            policy,
            "result-projection",
            ("! (Pair $ $)",),
            pair_result,
            pretty_vars=True,
        )
        expect(
            policy,
            "type-position",
            ("(: witness (-> $ $))", "! (get-type witness)"),
            type_result,
            pretty_vars=True,
        )

    for policy, one_rule_result, two_call_result in (
        ("literal", "[(Pair $ $)]", "[(Pair $ $)]"),
        ("fresh", "[(Pair $ $_1)]", "[(Pair $ $_1)]"),
        ("shared", "[(Pair $ $)]", "[(Pair $ $_1)]"),
    ):
        expect(
            policy,
            "rewrite-rhs-partition",
            ("(= (two-holes) (Pair $ $))", "! (two-holes)"),
            one_rule_result,
            pretty_vars=True,
        )
        expect(
            policy,
            "rewrite-application-freshening",
            ("(= (one-hole) $)", "! (Pair (one-hole) (one-hole))"),
            two_call_result,
            pretty_vars=True,
        )

    expect(
        "fresh",
        "generated-name-does-not-collide-with-future-user-name",
        ("! (Quad $ $_1 $ $_1)",),
        "[(Quad $ $_1 $_2 $_1)]",
        pretty_vars=True,
    )
    expect(
        "fresh",
        "generated-name-does-not-collide-with-prior-user-name",
        ("! (Quad $_1 $ $_1 $)",),
        "[(Quad $_1 $ $_1 $_2)]",
        pretty_vars=True,
    )

    # Existing named spellings retain their established co-reference laws.
    for policy in contenders:
        expect(
            policy,
            "named-variable",
            ("! (unify (P $x $x) (P a b) accepted rejected)",),
            "[rejected]",
        )
        expect(
            policy,
            "underscore-name",
            ("! (unify (P $_ $_) (P a b) accepted rejected)",),
            "[rejected]",
        )
        expect(
            policy,
            "structural-names",
            (
                "! (unify (P $@(left) $@(right)) (P a b) "
                "accepted rejected)",
            ),
            "[accepted]",
        )
        expect_syntax_rejection(
            policy,
            "he-isolation",
            ("! (unify (P $ $) (P a b) accepted rejected)",),
            language="he",
        )

    print(f"(PrimeBareDollarEvaluatorSummary {checks} {checks} 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
