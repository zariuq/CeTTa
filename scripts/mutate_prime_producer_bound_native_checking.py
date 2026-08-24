#!/usr/bin/env python3

import argparse
from pathlib import Path


MUTATIONS = {
    "scoped-route-disabled": (
        "if (cetta_prime_regular_kernel_unwrap_scoped(term, NULL, NULL)) {",
        "if (false &&\n"
        "        cetta_prime_regular_kernel_unwrap_scoped(term, NULL, NULL)) {",
    ),
    "declared-route-disabled": (
        "if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {\n"
        "        PrimeRegularTermCheckingDecision declared =",
        "if (false && CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {\n"
        "        PrimeRegularTermCheckingDecision declared =",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mutation", choices=sorted(MUTATIONS))
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    start = text.index("static Atom *prime_check_or_analyze(")
    end = text.index("static Atom *prime_form_judgment(", start)
    prefix = text[:start]
    checking = text[start:end]
    suffix = text[end:]

    old, new = MUTATIONS[args.mutation]
    count = checking.count(old)
    if count != 1:
        raise SystemExit(
            f"expected one {args.mutation} mutation site, found {count}")
    checking = checking.replace(old, new, 1)
    args.destination.write_text(prefix + checking + suffix, encoding="utf-8")


if __name__ == "__main__":
    main()
