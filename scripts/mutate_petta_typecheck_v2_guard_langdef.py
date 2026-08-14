#!/usr/bin/env python3
"""Create semantic mutations for the PeTTa typecheck-v2 guard langdef."""

from __future__ import annotations

import argparse
from pathlib import Path


MUTATIONS = {
    "actual-union-existential": (
        "(head (Consistent (TUnion ?ms) ?b EdgeStructural)) "
        "(body (ConsistentAllActual ?ms ?b))",
        "(head (Consistent (TUnion ?ms) ?b EdgeStructural)) "
        "(body (UnionMember ?m ?ms) (Consistent ?m ?b ?e))",
    ),
    "drop-alias-left": (
        "(head (Consistent (TNominal ?n) ?b EdgeStructural)) "
        "(body (EnvDeclared ?n (TAlias ?r)) (Consistent ?r ?b ?e))",
        "(head (Consistent (TNominal ?n) ?b EdgeStructural)) "
        "(body (EnvDeclared ?n (TNewtype ?r)) (Consistent ?r ?b ?e))",
    ),
    "drop-alias-right": (
        "(head (Consistent ?a (TNominal ?n) EdgeStructural)) "
        "(body (EnvDeclared ?n (TAlias ?r)) (Consistent ?a ?r ?e))",
        "(head (Consistent ?a (TNominal ?n) EdgeStructural)) "
        "(body (EnvDeclared ?n (TNewtype ?r)) (Consistent ?a ?r ?e))",
    ),
    "overlap-actual-union-universal": (
        "(head (MayOverlap (TUnion ?ms) ?b OverlapUnionLeft)) "
        "(body (UnionMember ?m ?ms) (MayOverlap ?m ?b ?e))",
        "(head (MayOverlap (TUnion ?ms) ?b OverlapUnionLeft)) "
        "(body (ConsistentAllActual ?ms ?b))",
    ),
    "drop-overlap-alias-left": (
        "(head (MayOverlap (TNominal ?n) ?b OverlapAliasLeft)) "
        "(body (EnvDeclared ?n (TAlias ?r)) (MayOverlap ?r ?b ?e))",
        "(head (MayOverlap (TNominal ?n) ?b OverlapAliasLeft)) "
        "(body (EnvDeclared ?n (TNewtype ?r)) (MayOverlap ?r ?b ?e))",
    ),
    "drop-overlap-alias-right": (
        "(head (MayOverlap ?a (TNominal ?n) OverlapAliasRight)) "
        "(body (EnvDeclared ?n (TAlias ?r)) (MayOverlap ?a ?r ?e))",
        "(head (MayOverlap ?a (TNominal ?n) OverlapAliasRight)) "
        "(body (EnvDeclared ?n (TNewtype ?r)) (MayOverlap ?a ?r ?e))",
    ),
    "drop-overlap-forward": (
        "(head (MayOverlap ?a ?b OverlapForward)) "
        "(body (Consistent ?a ?b ?e))",
        "(head (MayOverlap ?a ?b OverlapForwardMissing)) "
        "(body (Consistent ?a ?b ?e))",
    ),
    "drop-overlap-reverse": (
        "(head (MayOverlap ?a ?b OverlapReverse)) "
        "(body (Consistent ?b ?a ?e))",
        "(head (MayOverlap ?a ?b OverlapReverseMissing)) "
        "(body (Consistent ?b ?a ?e))",
    ),
    "drop-mode-det-semidet": (
        "(head (ModeFits MDet MSemidet)) (body)",
        "(head (ModeFits MDet MSemidetMissing)) (body)",
    ),
    "semidet-admits-nondet": (
        "(head (ModeFits MSemidet MSemidet)) (body)",
        "(head (ModeFits ?actual MSemidet)) (body)",
    ),
    "drop-mode-effect": (
        "(head (ModeFits ?actual (MEffect ?effect))) (body)",
        "(head (ModeFits ?actual (TBrand ?effect TNum))) (body)",
    ),
    "arrow-ignores-mode": (
        "(body (ModeFits ?actual-mode ?required-mode) "
        "(ConsistentList ?as ?bs) (Consistent ?r ?s ?e))",
        "(body (ConsistentList ?as ?bs) (Consistent ?r ?s ?e))",
    ),
    "arrow-ignores-components": (
        "(body (ModeFits ?actual-mode ?required-mode) "
        "(ConsistentList ?as ?bs) (Consistent ?r ?s ?e))",
        "(body (ModeFits ?actual-mode ?required-mode))",
    ),
    "dynamic-enters-newtype": (
        "(head (Consistent TUndefined ?t EdgeDynamic)) "
        "(body (DynamicMayFlowTo ?t))",
        "(head (Consistent TUndefined ?t EdgeDynamic)) (body)",
    ),
    "drop-newtype-representation": (
        "(body (EnvDeclared ?n (TNewtype ?r)) "
        "(ConcreteNewtypeRepresentation ?r) (Consistent ?r ?b ?e))",
        "(body (EnvDeclared ?n (TAlias ?r)) "
        "(ConcreteNewtypeRepresentation ?r) (Consistent ?r ?b ?e))",
    ),
    "newtype-wildcard-representation-universal": (
        "(body (EnvDeclared ?n (TNewtype ?r)) "
        "(ConcreteNewtypeRepresentation ?r) (Consistent ?r ?b ?e))",
        "(body (EnvDeclared ?n (TNewtype ?r)) (Consistent ?r ?b ?e))",
    ),
    "newtype-direction-reversed": (
        "(head (Consistent (TNominal ?n) ?b EdgeStructural)) "
        "(body (EnvDeclared ?n (TNewtype ?r)) "
        "(ConcreteNewtypeRepresentation ?r) (Consistent ?r ?b ?e))",
        "(head (Consistent ?b (TNominal ?n) EdgeStructural)) "
        "(body (EnvDeclared ?n (TNewtype ?r)) "
        "(ConcreteNewtypeRepresentation ?r) (Consistent ?b ?r ?e))",
    ),
    "collapse-newtype-identity": (
        "(head (Consistent ?t ?t EdgeExact)) (body)",
        "(head (Consistent (TNominal ?n) (TNominal ?m) EdgeExact)) "
        "(body)",
    ),
    "drop-expression-effect-foldall": (
        "(head (ExpressionEffect\n"
        "        (EFoldall ?accumulator ?generator ?initial) MDet)) (body)",
        "(head (ExpressionEffect\n"
        "        (EFoldall ?accumulator ?generator ?initial) "
        "MSemidet)) (body)",
    ),
    "drop-expression-effect-collapse": (
        "(head (ExpressionEffect (ECollapse ?expression) MDet)) (body)",
        "(head (ExpressionEffect (ECollapse ?expression) MSemidet)) (body)",
    ),
    "drop-expression-result-type-known": (
        "(head (ExpressionResultType ?expression ?type))\n"
        "      (body (KnownExpressionResultType ?expression ?type))",
        "(head (ExpressionResultType ?expression ?type))\n"
        "      (body (KnownExpressionEffect ?expression ?type))",
    ),
    "drop-expression-result-type-collapse": (
        "(head (ExpressionResultType (ECollapse ?expression) (TList ?type)))\n"
        "      (body (ExpressionResultType ?expression ?type))",
        "(head (ExpressionResultType (ECollapse ?expression) "
        "(TBrand Missing ?type)))\n"
        "      (body (ExpressionResultType ?expression ?type))",
    ),
    "drop-expression-result-type-foldall-empty": (
        "(head (ExpressionResultType\n"
        "        (EFoldall ?accumulator EEmpty ?initial) ?type))\n"
        "      (body (ExpressionResultType ?initial ?type))",
        "(head (ExpressionResultType\n"
        "        (EFoldall ?accumulator EEmpty ?initial) TUndefined))\n"
        "      (body (ExpressionResultType ?initial ?type))",
    ),
    "drop-expression-effect-once-det": (
        "(head (ExpressionEffect (EOnce ?expression) MDet))\n"
        "      (body (ExpressionEffect ?expression MDet))",
        "(head (ExpressionEffect (EOnce ?expression) MSemidet))\n"
        "      (body (ExpressionEffect ?expression MDet))",
    ),
    "drop-expression-effect-once-semidet": (
        "(head (ExpressionEffect (EOnce ?expression) MSemidet))\n"
        "      (body (ExpressionEffect ?expression MSemidet))",
        "(head (ExpressionEffect (EOnce ?expression) MDet))\n"
        "      (body (ExpressionEffect ?expression MSemidet))",
    ),
    "drop-expression-effect-once-nondet": (
        "(head (ExpressionEffect (EOnce ?expression) MSemidet))\n"
        "      (body (ExpressionEffect ?expression MNondet))",
        "(head (ExpressionEffect (EOnce ?expression) MDet))\n"
        "      (body (ExpressionEffect ?expression MNondet))",
    ),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mutation", choices=sorted(MUTATIONS))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()

    source = arguments.source.read_text(encoding="utf-8")
    old, new = MUTATIONS[arguments.mutation]
    if source.count(old) != 1:
        parser.error(
            f"mutation anchor count changed for {arguments.mutation}"
        )
    arguments.output.write_text(source.replace(old, new, 1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
