#!/usr/bin/env python3
"""Positive and negative discriminators for Prime bare-``$`` semantics."""

from __future__ import annotations

from bare_dollar_reference import (
    Constant,
    Dollar,
    Elaborator,
    FreeVariable,
    LetSurface,
    Named,
    NoMatch,
    PairSurface,
    PairValue,
    Policy,
    QuoteSurface,
    Quoted,
    Reference,
    StructuralNamed,
    Symbol,
    elaborate_and_evaluate,
    match,
    projected_named_bindings,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def reference_match_matrix() -> dict[str, bool]:
    result: dict[str, bool] = {}
    for policy in Policy:
        pattern = Elaborator(policy).term(Dollar())
        bindings = {}
        result[policy.value] = match(pattern, Reference("self"), bindings)
        if result[policy.value]:
            require(
                projected_named_bindings(pattern, bindings) == {},
                f"{policy.value}: anonymous binding leaked into named projection",
            )
    return result


def main() -> int:
    checks = 0

    reference_matches = reference_match_matrix()
    require(reference_matches == {
        "literal": False,
        "fresh": True,
        "shared": True,
        "root-binder": True,
    }, f"grounded-reference matrix drifted: {reference_matches}")
    checks += 4

    unequal = PairValue(Symbol("a"), Symbol("b"))
    multi_ignore: dict[str, bool] = {}
    for policy in Policy:
        pattern = Elaborator(policy).term(PairSurface(Dollar(), Dollar()))
        multi_ignore[policy.value] = match(pattern, unequal, {})
    require(multi_ignore == {
        "literal": False,
        "fresh": True,
        "shared": False,
        "root-binder": True,
    }, f"independent-ignore matrix drifted: {multi_ignore}")
    checks += 4

    reference = Reference("workspace")
    bind_then_use = LetSurface(Dollar(), Constant(reference), Dollar())
    binder_results = {
        policy.value: elaborate_and_evaluate(policy, bind_then_use)
        for policy in Policy
    }
    require(isinstance(binder_results["literal"], NoMatch),
            "literal contender unexpectedly matched a reference")
    require(isinstance(binder_results["fresh"], FreeVariable),
            "fresh contender unexpectedly made a discard slot referenceable")
    require(binder_results["shared"] == reference,
            "shared contender did not reuse its empty-name binding")
    require(binder_results["root-binder"] == reference,
            "root-binder contender did not pass the grounded reference")
    checks += 4

    structured_pattern = LetSurface(
        PairSurface(Dollar(), Dollar()),
        Constant(unequal),
        Constant(Symbol("accepted")),
    )
    require(
        elaborate_and_evaluate(Policy.FRESH, structured_pattern) ==
            Symbol("accepted"),
        "fresh contender overconstrained independent discard slots",
    )
    require(
        elaborate_and_evaluate(Policy.ROOT_BINDER, structured_pattern) ==
            Symbol("accepted"),
        "root-binder contender overconstrained nested discard slots",
    )
    require(
        isinstance(elaborate_and_evaluate(Policy.SHARED, structured_pattern), NoMatch),
        "shared contender failed to expose its repeated-slot equality constraint",
    )
    checks += 3

    nested = LetSurface(
        Dollar(),
        Constant(Symbol("outer")),
        PairSurface(
            LetSurface(Dollar(), Constant(Symbol("inner")), Dollar()),
            Dollar(),
        ),
    )
    require(
        elaborate_and_evaluate(Policy.ROOT_BINDER, nested) ==
            PairValue(Symbol("inner"), Symbol("outer")),
        "nested root binders did not shadow deterministically",
    )
    checks += 1

    quoted = LetSurface(
        Dollar(), Constant(reference), QuoteSurface(Dollar())
    )
    require(
        elaborate_and_evaluate(Policy.ROOT_BINDER, quoted) == Quoted(Dollar()),
        "quotation captured or elaborated the anonymous binder",
    )
    checks += 1

    for variable in (Named("space"), StructuralNamed(("space", "workspace"))):
        named_program = LetSurface(variable, Constant(reference), variable)
        for policy in Policy:
            require(
                elaborate_and_evaluate(policy, named_program) == reference,
                f"{policy.value}: named-variable behavior changed",
            )
            checks += 1

    repeated_named = LetSurface(
        PairSurface(Named("x"), Named("x")),
        Constant(unequal),
        Constant(Symbol("impossible")),
    )
    for policy in Policy:
        require(
            isinstance(elaborate_and_evaluate(policy, repeated_named), NoMatch),
            f"{policy.value}: repeated named variable lost its equality constraint",
        )
        checks += 1

    print(
        f"(PrimeBareDollarReferenceSummary {checks} {checks} 0 "
        f"reference={reference_matches} multi-ignore={multi_ignore})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
