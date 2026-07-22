#!/usr/bin/env python3
"""Whole-evaluator discriminators for Prime bare-``$`` contenders.

The public fresh reader and alternate literal/shared readers are compared
through their complete evaluators.  This checks that their identity partitions
survive parsing, rewriting, evaluation, quotation, typing syntax, result
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


def invoke(
    contender: Contender,
    expressions: tuple[str, ...],
    *,
    language: str = "prime",
    pretty_vars: bool = False,
) -> str:
    command = [str(contender.binary), "--lang", language]
    if language == "he":
        command.extend(("--profile", "he-compat"))
    if pretty_vars:
        command.append("--pretty-vars")
    for expression in expressions:
        command.extend(("-e", expression))
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
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
            ("! (let $ &self (noreduce-eq $ &self))",),
            reused,
        )

    for policy, expected in (("fresh", "[False]"), ("shared", "[True]")):
        expect(
            policy,
            "chain-binder-referenceability",
            ("! (chain &self $ (noreduce-eq $ &self))",),
            expected,
        )
        expect(
            policy,
            "let-star-binder-referenceability",
            ("! (let* (($ &self)) (noreduce-eq $ &self))",),
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

    # Native substitution has two observable classes.  Lexical ABT binders
    # (`let` and `let*`) treat quote as a scope barrier.  Matcher/template
    # substitution (`chain`, `unify`, `match`, and rewrite application) may
    # instantiate matching identities below quote.  Named variables pin this
    # distinction independently of the bare-dollar contenders.
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
            "quoted-named-chain-binder",
            ("! (chain a $x @$x)",),
            "[(quote a)]",
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-named-unifier-binding",
            ("! (unify $x a @$x no)",),
            "[(quote a)]",
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
            "[(quote a)]",
            pretty_vars=True,
        )
        expect(
            policy,
            "quoted-named-rewrite-binding",
            ("(= (quote-named $x) @$x)", "! (quote-named a)"),
            "[(quote a)]",
            pretty_vars=True,
        )

    # This is characterization evidence for the root-binder decision, not a
    # claim that shared bare-dollar behavior should be promoted.  A uniformly
    # fresh anonymous occurrence never names the earlier pattern occurrence,
    # so neither substitution class can accidentally recover its binding.
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
            "[(quote a)]",
            "[(quote a)]",
            "[(quote a)]",
            "[(quote a)]",
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

    # Unquoting makes the distinction visible as ordinary syntax.  Lexical
    # substitution still cannot retroactively cross its quote barrier;
    # matcher substitution has already instantiated the quoted variable.
    for policy, lexical, matcher in (
        ("fresh", "[$]", "[$]"),
        ("shared", "[$]", "[a]"),
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
        expect(
            policy,
            "he-isolation",
            ("! (unify (P $ $) (P a b) accepted rejected)",),
            "[rejected]",
            language="he",
        )

    print(f"(PrimeBareDollarEvaluatorSummary {checks} {checks} 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
