"""Translate declaration-order type parameters to nested de Bruijn binders.

Megalodon's source export uses TPVAR 0 for the first supplied type parameter
(syntax.ml's tpsubst indexes the supplied argument list directly). The NIK
Mathdata representation uses explicit nested type binders, whose index zero
is the last parameter. Reverse only the declared interval, at its current
type-binder depth. Object binders and proof-hypothesis indices are unchanged.

This is source translation, not proof acceptance. The ordered NIK checker
still checks the translated types, terms and proof articles independently.
"""

import gslt2parse_schema_v1 as sx


def to_binders(value: sx.SExpr, count: int, depth: int = 0) -> sx.SExpr:
    if count < 0 or depth < 0:
        raise ValueError("type-prefix size and depth must be nonnegative")
    if not isinstance(value, tuple) or not value:
        return value
    head = value[0].text if isinstance(value[0], sx.Symbol) else None
    if head == "TPVAR" and len(value) == 2 and type(value[1]) is int:
        index = value[1]
        if depth <= index < depth + count:
            return (value[0], depth + count - 1 - (index - depth))
        return value
    # TALL binds a type variable in types or propositions. TLAM with one
    # argument is a type abstraction; proof TLAM has a type and a body and
    # instead binds an object variable.
    if head in {"TALL", "TLAM"} and len(value) == 2:
        return (value[0], to_binders(value[1], count, depth + 1))
    return tuple(to_binders(child, count, depth) for child in value)
