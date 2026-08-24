#!/usr/bin/env python3

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--mutation",
        choices=(
            "route-disabled",
            "budget-coupled-recognizer",
            "dependent-graphs-disabled",
            "constructed-terms-disabled",
        ),
        default="route-disabled",
    )
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    start = text.index("static Atom *prime_convert(")
    end = text.index("static Atom *prime_refine(", start)
    prefix = text[:start]
    conversion = text[start:end]
    suffix = text[end:]

    if args.mutation == "route-disabled":
        old = (
            "if (CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {\n"
            "        Atom *native = prime_convert_declared_regular(")
        new = (
            "if (false && CETTA_PRIME_REGULAR_KERNEL_NATIVE_ADMISSION_ACTIVE) {\n"
            "        Atom *native = prime_convert_declared_regular(")
        count = conversion.count(old)
        if count != 1:
            raise SystemExit(
                f"expected one declared conversion route, found {count}")
        conversion = conversion.replace(old, new, 1)
    elif args.mutation == "budget-coupled-recognizer":
        declared_start = prefix.index(
            "static Atom *prime_convert_declared_regular(")
        declared_prefix = prefix[declared_start:]
        old = (
            "CettaPrimeRegularKernelBudget recognition_budget;\n"
            "    cetta_prime_regular_kernel_budget_init(\n"
            "        &recognition_budget, true, UINT64_MAX);")
        new = (
            "CettaPrimeRegularKernelBudget recognition_budget =\n"
            "        prime_regular_kernel_budget(ledger);")
        count = declared_prefix.count(old)
        if count != 1:
            raise SystemExit(
                f"expected one conversion recognizer budget, found {count}")
        declared_prefix = declared_prefix.replace(old, new, 1)
        prefix = prefix[:declared_start] + declared_prefix
    elif args.mutation == "dependent-graphs-disabled":
        old = "if (prime_regular_declaration_trail_contains(trail, name))"
        new = (
            "if (trail || "
            "prime_regular_declaration_trail_contains(trail, name))"
        )
        count = prefix.count(old)
        if count != 1:
            raise SystemExit(
                f"expected one dependency-trail boundary, found {count}")
        prefix = prefix.replace(old, new, 1)
    else:
        old = (
            "    if (!space || !arena || !term || !declarations || !budget)\n"
            "        return (PrimeRegularDeclaredElaboration){0};\n\n"
            "    for (;;) {")
        new = (
            "    if (!space || !arena || !term || !declarations || !budget)\n"
            "        return (PrimeRegularDeclaredElaboration){0};\n"
            "    if (term->kind == ATOM_EXPR)\n"
            "        return (PrimeRegularDeclaredElaboration){0};\n\n"
            "    for (;;) {")
        count = prefix.count(old)
        if count != 1:
            raise SystemExit(
                f"expected one declared-term recognizer, found {count}")
        prefix = prefix.replace(old, new, 1)
    args.destination.write_text(prefix + conversion + suffix, encoding="utf-8")


if __name__ == "__main__":
    main()
