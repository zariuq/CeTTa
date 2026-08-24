#!/usr/bin/env python3

import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} SOURCE OUTPUT", file=sys.stderr)
        return 2

    source = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])
    text = source.read_text()
    needle = """    if (regular_expr(term, \"App\", 3u) || regular_expr(term, \"Pair\", 3u)) {
        return regular_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               regular_scope_check(
                   term->expr.elems[2], depth, budget, complete);
    }
    if (regular_expr(term, \"Fst\", 2u) || regular_expr(term, \"Snd\", 2u) ||
        regular_expr(term, \"Refl\", 2u)) {
        return regular_scope_check(
            term->expr.elems[1], depth, budget, complete);
    }
    if (regular_expr(term, \"Id\", 4u)) {
        return regular_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               regular_scope_check(
                   term->expr.elems[2], depth, budget, complete) &&
               regular_scope_check(
                   term->expr.elems[3], depth, budget, complete);
    }
    return false;
}"""
    replacement = """    if (regular_expr(term, \"App\", 3u) || regular_expr(term, \"Pair\", 3u)) {
        return regular_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               regular_scope_check(
                   term->expr.elems[2], depth, budget, complete);
    }
    if (regular_expr(term, \"Fst\", 2u) || regular_expr(term, \"Snd\", 2u) ||
        regular_expr(term, \"Refl\", 2u)) {
        return regular_scope_check(
            term->expr.elems[1], depth, budget, complete);
    }
    if (regular_expr(term, \"Id\", 4u)) {
        return regular_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               regular_scope_check(
                   term->expr.elems[2], depth, budget, complete) &&
               regular_scope_check(
                   term->expr.elems[3], depth, budget, complete);
    }
    return true;
}"""
    if text.count(needle) != 1:
        print("recognizer mutation anchor is absent or ambiguous", file=sys.stderr)
        return 1
    output.write_text(text.replace(needle, replacement, 1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
