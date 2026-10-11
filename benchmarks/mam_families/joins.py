"""Finite ground joins and an independent indexed binary-loop oracle."""
from collections import defaultdict
from dataclasses import dataclass


@dataclass(frozen=True)
class Factor:
    head: str
    variables: tuple
    rows: tuple


def occurrence_join(factors):
    """Source-ordered row loops with lookups on already bound columns.

    No variable intersection, sorted range navigation or grouped cardinality
    is used here. Each physical row produces a separate occurrence witness.
    """
    lookups = []
    known = set()
    for factor in factors:
        assert all(len(row) == len(factor.variables) for row in factor.rows)
        positions = [i for i, variable in enumerate(factor.variables) if variable in known]
        index = defaultdict(list)
        for identity, row in enumerate(factor.rows):
            values = {}
            consistent = True
            for variable, value in zip(factor.variables, row):
                if variable in values and values[variable] != value:
                    consistent = False
                values[variable] = value
            if consistent:
                index[tuple(row[i] for i in positions)].append((identity, values))
        lookups.append((positions, index))
        known.update(factor.variables)

    def visit(level, bindings, witness):
        if level == len(factors):
            yield witness
            return
        factor = factors[level]
        positions, index = lookups[level]
        key = tuple(bindings[factor.variables[i]] for i in positions)
        for identity, values in index[key]:
            yield from visit(level + 1, bindings | values, witness + (identity,))
    return visit(0, {}, ())


def declarations(factors):
    # Row IDs retain bag occurrences when stored in MORK's set representation.
    return "".join(f"({factor.head} {' '.join(map(str, row))} {identity})\n"
                   for factor in factors for identity, row in enumerate(factor.rows))


def query(factors):
    return "(, " + " ".join(
        f"({factor.head} {' '.join('$' + variable for variable in factor.variables)} $o{index})"
        for index, factor in enumerate(factors)) + ")"


def metta_count(language, factors):
    items = f"(collapse (match &self {query(factors)} 1))"
    count = f"(let $items {items} (size-atom $items))" if language == "he" \
        else f"(size-atom {items})"
    return declarations(factors) + f"!{count}\n"


def witness(factors):
    return "(join-witness " + " ".join(f"$o{i}" for i in range(len(factors))) + ")"


def metta_witnesses(factors):
    return declarations(factors) + f"!(match &self {query(factors)} {witness(factors)})\n"


def mm2_count(factors):
    identities = "(" + " ".join(f"$o{i}" for i in range(len(factors))) + ")"
    return declarations(factors) + (
        f"(exec (0 join-count) {query(factors)} "
        f"(O (count (join-count $n) $n {identities})))\n")


def mm2_witnesses(factors):
    return declarations(factors) + (
        f"(exec (0 join-witness) {query(factors)} (O (+ {witness(factors)})))\n")
