#!/usr/bin/env python3
"""Independent oracles for the frozen symbolic ladder.

Graph answers are nested matches in fact order. The other families are the
small programs' own observation, not a second MeTTa interpreter.
"""

from __future__ import annotations


def paths(edges, src=None):
    out = []
    for x, y in edges:
        if src is not None and x != src:
            continue
        for y2, z in edges:
            if y2 == y:
                out.append((x, y, z))
    return out


def stars(edges):
    out = []
    for c, a in edges:
        for c2, b in edges:
            if c2 == c:
                out.append((c, a, b))
    return out


def diamonds(edges):
    out = []
    for s, a in edges:
        for s2, b in edges:
            if s2 != s:
                continue
            for a2, t in edges:
                if a2 != a:
                    continue
                for b2, t2 in edges:
                    if b2 == b and t2 == t:
                        out.append((s, a, b, t))
    return out


def backs(edges):
    out = []
    for x, y in edges:
        for y2, x2 in edges:
            if y2 == y and x2 == x:
                out.append((x, y))
    return out


def loops(edges):
    return [(x,) for x, y in edges if x == y]


def bad_loops(edges):
    out = []
    for x, y in edges:
        if x != y:
            continue
        for x2, z in edges:
            if x2 == x:
                out.append((x, z))
    return out


def proofs(edges, src):
    out = []
    for x, y in edges:
        if x != src:
            continue
        for y2, z in edges:
            if y2 == y:
                out.append((src, y, z))
    return out


def fmt_edge_tuple(head, values):
    return "(" + head + " " + " ".join(values) + ")"


def graph_witness_small():
    edges = [("a", "b"), ("b", "c"), ("b", "d")]
    return [fmt_edge_tuple("", p).replace("( ", "(") for p in []] or [
        f"({x} {y} {z})" for x, y, z in paths(edges, "a")
    ]


def graph_counts():
    small = [("a", "b"), ("b", "c"), ("b", "d")]
    dup = [("a", "b"), ("a", "b"), ("b", "c")]
    miss = [("a", "b"), ("c", "d")]
    return {
        "graph_count_small": str(len(paths(small))),
        "graph_count_duplicate": str(len(paths(dup))),
        "graph_count_miss": str(len(paths(miss))),
    }


def triangles(edges):
    out = []
    for a, b in edges:
        for b2, c in edges:
            if b2 != b:
                continue
            for c2, a2 in edges:
                if c2 == c and a2 == a:
                    out.append((a, b, c))
    return out


def graph_shapes():
    edges = [
        ("s", "a"),
        ("s", "b"),
        ("a", "t"),
        ("b", "t"),
        ("a", "b"),
        ("b", "a"),
        ("a", "a"),
    ]
    lines = []
    for c, a, b in stars(edges):
        lines.append(f"(star {c} {a} {b})")
    for a, b, c in triangles(edges):
        lines.append(f"(tri {a} {b} {c})")
    for s, a, b, t in diamonds(edges):
        lines.append(f"(dia {s} {a} {b} {t})")
    for x, y in backs(edges):
        lines.append(f"(back {x} {y})")
    for (x,) in loops(edges):
        lines.append(f"(loop {x})")
    for x, z in bad_loops(edges):
        lines.append(f"(bad {x} {z})")
    lines.append("(kept s a)")
    lines.append("(h a (lbl p) b)")
    for _src, y, z in proofs(edges, "a"):
        lines.append(f"(proof a {y} {z})")
    return lines


def hm_small():
    # var x is int. Applying int fails. lam body y is bool, argument x is int.
    return ["int", "bool"]


def parse_small():
    # one and four are balanced; three is the failed branch; two is the empty word.
    return ["(one (mark end))", "(two (mark end))", "(four (mark end))"]


def join_caller_bind():
    lefts = ["a", "b"]
    rights = [("a", "old"), ("b", "other")]
    lines = []
    for x in lefts:
        for rx, y in rights:
            if rx == x:
                lines.append(f"(out {x} {y})")
    return lines


def join_mutate_visible():
    """Nested match, logical update.

    Each inner leg snapshots the rights visible when that leg begins.
    The continuation inserts (right b new) after an answer, so the next
    leg sees it. The current leg's snapshot does not grow.
    """
    lefts = ["a", "b"]
    rights = [("a", "old"), ("b", "old")]
    lines = []
    for x in lefts:
        snapshot = list(rights)
        for rx, y in snapshot:
            if rx != x:
                continue
            lines.append(f"(w {x} {y})")
            rights.append(("b", "new"))
    return lines


def fold_item_acc(operation, items, init):
    """PeTTa foldall applies (operation item accumulator), in fact order."""
    accumulator = init
    for item in items:
        accumulator = operation(item, accumulator)
    return accumulator


def fold_arith():
    edges = [2, 2, 2]
    signed = [-3, -3]
    weights = [3, 3, 10]
    safe_mod = [7, 5]
    lines = [
        fold_item_acc(lambda item, acc: item + acc, edges, 3),
        fold_item_acc(lambda item, acc: item - acc, edges, 20),
        fold_item_acc(lambda item, acc: item * acc, edges, 3),
        5,
        5,
        8,
        fold_item_acc(lambda item, acc: item - acc, signed, 1),
        fold_item_acc(lambda item, acc: item % acc, [3, 3], -5),
        fold_item_acc(lambda item, acc: item * acc, [0, 0], 4),
        fold_item_acc(lambda item, acc: item * acc, [1, 1], -8),
        fold_item_acc(lambda item, acc: item * acc, [-1, -1], 6),
        fold_item_acc(lambda item, acc: item % acc, [-3], 5),
        fold_item_acc(lambda item, acc: item + acc, weights, 1),
        fold_item_acc(lambda item, acc: item * acc, weights, 1),
        fold_item_acc(lambda item, acc: item - acc, weights, 100),
        10,
        fold_item_acc(lambda item, acc: item + acc, [5, 5, 5, 5], 0),
        1,
        2,
        fold_item_acc(lambda item, acc: acc - item, [4, 4], 10),
        fold_item_acc(lambda item, acc: acc - item, [4], 10),
        fold_item_acc(lambda item, acc: item + 1, [1, 1], 7),
        fold_item_acc(lambda item, acc: item % acc, [4, 4], 23),
        fold_item_acc(lambda item, acc: item % acc, safe_mod, 100),
        fold_item_acc(lambda item, acc: acc, weights, 0),
    ]
    return [str(line) for line in lines]


def residual_alias():
    # (alias 1 2) fails the repeated variable.
    # (alias $y a) binds both sides to a.
    # (alias 1 1) binds both sides to 1.
    return ["yes", "yes"]


def rollback_branch():
    # The failed match drops the binding of $x. The next equation answers.
    return ["kept"]


def fresh_pick():
    # Each equation mints its own $x. The spelling is not an alias.
    return ["a", "b"]


def graph_bound():
    # Ground source a. Outgoing targets stay in fact order.
    return ["b", "c"]


def handoff_if():
    # True, False, then any other non-empty value, then the next equation.
    return ["yes", "no", "no", "later"]


def handoff_open():
    letters = ["a", "a", "b", "z"]
    edges = [("a", "b"), ("a", "d"), ("b", "c")]
    proofs = list(letters) + ["a"]
    lines = []
    for name in proofs:
        for left, right in edges:
            if left == name:
                lines.append(f"(wit {name} {right})")
    return lines


def reach_small():
    succ = [("1", "2"), ("2", "3")]
    assign = [("1", "x"), ("1", "y")]
    lines = []
    for src, node in succ:
        for src2, var in assign:
            if src2 == src:
                lines.append(f"(rd {node} {src} {var})")
    lines.append("(killed 2 x)")
    return lines


def main():
    import sys
    from pathlib import Path

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import families

    sections = {
        "graph_witness_small": graph_witness_small(),
        "graph_shapes": graph_shapes(),
        "hm_small": hm_small(),
        "parse_small": parse_small(),
        "reach_small": reach_small(),
        "join_caller_bind": join_caller_bind(),
        "join_mutate": join_mutate_visible(),
        "join_mutate_template": join_mutate_visible(),
        "residual_alias": residual_alias(),
        "rollback_branch": rollback_branch(),
        "fresh_pick": fresh_pick(),
        "graph_bound": graph_bound(),
        "handoff_if": handoff_if(),
        "handoff_open": handoff_open(),
        "fold_arith": fold_arith(),
    }
    for stem, _family, _program, answers in families.CASES:
        sections[stem] = answers()
    sections.update({name: [value] for name, value in graph_counts().items()})
    for name, lines in sections.items():
        print(f"## {name}")
        print("\n".join(lines))
        print()


if __name__ == "__main__":
    main()
