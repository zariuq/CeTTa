#!/usr/bin/env python3

import argparse
from pathlib import Path


MUTATIONS = {
    "formed-position-refutes": (
        """        if (reason_out) *reason_out = \"expected-formed-type\";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }
    if (sort_out) *sort_out = inferred.type;
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;""",
        """        if (reason_out) *reason_out = \"expected-formed-type\";
        return CETTA_PRIME_REGULAR_KERNEL_REFUTED;
    }
    if (sort_out) *sort_out = inferred.type;
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;""",
    ),
    "universe-synthesis-refutes": (
        """        Atom *successor = regular_sort_successor(arena, term);
        return successor
            ? regular_infer_result(
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
                  successor, true, NULL)""",
        """        Atom *successor = regular_sort_successor(arena, term);
        return successor
            ? regular_infer_result(
                  CETTA_PRIME_REGULAR_KERNEL_REFUTED,
                  successor, true, NULL)""",
    ),
    "universe-family-refutes": (
        """        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            joined_sort, true, NULL);
    }
    if (regular_expr(term, \"Lam\", 2u))""",
        """        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_REFUTED,
            joined_sort, true, \"tower-family-refuted\");
    }
    if (regular_expr(term, \"Lam\", 2u))""",
    ),
    "refl-of-type-refutes": (
        """        if (reflected.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return reflected;
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,""",
        """        if (reflected.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return reflected;
        if (reflected.type_is_sort)
            return regular_infer_result(
                CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL, false,
                \"tower-reflexivity-refuted\");
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,""",
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
