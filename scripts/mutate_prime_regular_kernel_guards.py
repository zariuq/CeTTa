#!/usr/bin/env python3

import argparse
from pathlib import Path


MUTATIONS = {
    "closed-guard-dropped": (
        """        return regular_term_closed(term, 0u) &&
               regular_rule_match(pat->expr.elems[1], term, binds, nvars);""",
        """        return regular_rule_match(pat->expr.elems[1], term, binds, nvars);""",
    ),
    "observation-dropped": (
        """            arity != argc || waits != observed)""",
        """            arity != argc || (observed && !waits))""",
    ),
    "guarded-refutes": (
        """    bool reached = regular_reaches_guarded(term, &search) || search.full;""",
        """    bool reached = false;""",
    ),
    "reached-guard-dropped": (
        """    if (regular_name_guarded(name)) return true;
    for (Atom *r = g_regular_rules; regular_expr(r, "LCons", 3u); r = r->expr.elems[2]) {
        Atom *rule = r->expr.elems[1];
        if (regular_expr(rule, "PrimeRule", 5u) && atom_eq(rule->expr.elems[1], name) &&
            regular_reaches_guarded(rule->expr.elems[4], search))
            return true;
    }""",
        """    if (regular_name_guarded(name)) return true;""",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mutation", choices=sorted(MUTATIONS))
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    old, new = MUTATIONS[args.mutation]
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"expected one {args.mutation} mutation site, found {count}")
    args.destination.write_text(text.replace(old, new, 1), encoding="utf-8")


if __name__ == "__main__":
    main()
