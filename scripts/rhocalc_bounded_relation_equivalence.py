#!/usr/bin/env python3

import sys

from rhocalc_bounded_reachability import exact_depth_reachable


def main() -> int:
    if len(sys.argv) != 7:
        print(
            "usage: rhocalc_bounded_relation_equivalence.py "
            "<bin> <depth> <lhs-syntax> <lhs-file> <rhs-syntax> <rhs-file>",
            file=sys.stderr,
        )
        return 2

    bin_path = sys.argv[1]
    depth = int(sys.argv[2])
    lhs_syntax = sys.argv[3]
    lhs_path = sys.argv[4]
    rhs_syntax = sys.argv[5]
    rhs_path = sys.argv[6]

    if depth < 1:
        print("depth must be at least 1", file=sys.stderr)
        return 2

    try:
        for current_depth in range(1, depth + 1):
            lhs = exact_depth_reachable(bin_path, lhs_syntax, current_depth, lhs_path)
            rhs = exact_depth_reachable(bin_path, rhs_syntax, current_depth, rhs_path)
            if lhs == rhs:
                continue
            only_lhs = sorted(lhs - rhs)
            only_rhs = sorted(rhs - lhs)
            print(
                f"exact-depth reachable sets differ at depth {current_depth}",
                file=sys.stderr,
            )
            print(f"lhs-count: {len(lhs)}", file=sys.stderr)
            print(f"rhs-count: {len(rhs)}", file=sys.stderr)
            if only_lhs:
                print("only-lhs:", file=sys.stderr)
                for item in only_lhs:
                    print(item, file=sys.stderr)
            if only_rhs:
                print("only-rhs:", file=sys.stderr)
                for item in only_rhs:
                    print(item, file=sys.stderr)
            return 1
    except (RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
