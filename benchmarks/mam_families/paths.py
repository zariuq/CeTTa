"""Finite path workloads and an independent occurrence-enumeration oracle."""
from dataclasses import dataclass


@dataclass(frozen=True)
class Row:
    source: int
    target: int
    value: int


def occurrence_paths(relations):
    """Nested occurrence loops; no grouped-message algorithm or engine calls."""
    def visit(level, key, path):
        if level == len(relations):
            yield path
            return
        for occurrence, row in enumerate(relations[level]):
            if level == 0 or row.source == key:
                yield from visit(level + 1, row.target,
                                 path + ((row, occurrence),))
    return visit(0, None, ())


def enumerate_values(relations, column):
    return [path[column][0].value for path in occurrence_paths(relations)]


def enumerate_witnesses(relations):
    return [tuple(occurrence for _, occurrence in path)
            for path in occurrence_paths(relations)]


def declarations(relations):
    # Distinct occurrence fields retain equal payloads in a set store too.
    return "".join(f"(path {level} {row.source} {row.target} {row.value} {index})\n"
                   for level, rows in enumerate(relations)
                   for index, row in enumerate(rows))


def pattern(levels):
    return "(, " + " ".join(
        f"(path {level} $k{level} $k{level+1} $v{level} $o{level})"
        for level in range(levels)) + ")"


def fold(language, query, template, initial=0, operation="+"):
    stream = f"(match &self {query} {template})"
    if language == "petta":
        return f"!(foldall {operation} {stream} {initial})\n"
    assert language == "he" and operation == "+"
    return f"!(fold {stream} {initial} $acc $item (eval (+ $acc $item)))\n"


def witness_template(levels):
    return "(path-witness " + " ".join(f"$o{i}" for i in range(levels)) + ")"


def metta_count(language, relations, embedded=False, cardinality=False):
    query = pattern(len(relations))
    if cardinality:
        collection = f"(collapse (match &self {query} 1))"
        # HE size-atom observes syntax; the let makes the collection a value.
        expression = f"(let $items {collection} (size-atom $items))" if language == "he" \
            else f"(size-atom {collection})"
        computation = f"!{expression}\n"
    else:
        computation = fold(language, query, "1")
    if embedded:
        computation = f"!(path-count {computation[1:].strip()})\n"
    return declarations(relations) + computation


def metta_witnesses(relations):
    return declarations(relations) + (
        f"!(match &self {pattern(len(relations))} "
        f"{witness_template(len(relations))})\n")


def mm2_count(relations):
    # The count sink counts distinct witness keys, so keep every row identity.
    identities = "(" + " ".join(f"$o{i}" for i in range(len(relations))) + ")"
    return declarations(relations) + (
        f"(exec (0 path-count) {pattern(len(relations))} "
        f"(O (count (path-count $n) $n {identities})))\n")


def mm2_witnesses(relations):
    return declarations(relations) + (
        f"(exec (0 path-witness) {pattern(len(relations))} "
        f"(O (+ {witness_template(len(relations))})))\n")


def mm2_independent_count(relations):
    """Separate counts then multiply, when every separator is the fixed key 0.

    This concrete MM2 grounding uses i64 arithmetic; the manifest limits counts
to its exact range. The general correlated-path alternative is mm2_count.
"""
    assert relations and all(rows for rows in relations)
    assert all(row.source == row.target == 0 for rows in relations for row in rows)
    count = 1
    product = "(i64_from_string $c0)"
    for level, rows in enumerate(relations):
        count *= len(rows)
        if level:
            product = f"(product_i64 {product} (i64_from_string $c{level}))"
    assert count < 2 ** 63
    messages = "(, " + " ".join(f"(level-count {i} $c{i})"
                                   for i in range(len(relations))) + ")"
    return declarations(relations) + (
        "(exec (0 counts) (, (path $level 0 0 $value $occurrence)) "
        "(O (count (level-count $level $count) $count $occurrence)))\n"
        f"(exec (1 product) {messages} "
        f"(O (pure (path-count $result) $result "
        f"(i64_to_string {product}))))\n")
