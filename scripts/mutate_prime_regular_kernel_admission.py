#!/usr/bin/env python3

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one {label} mutation site, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "mutation",
        choices=(
            "receipt",
            "generation",
            "synthesis-receipt",
            "synthesis-generation",
            "checking-receipt",
            "checking-generation",
            "collection",
            "synthesis-collection",
            "checking-collection",
        ),
    )
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    if args.mutation in (
        "receipt", "synthesis-receipt", "checking-receipt"):
        function_name = (
            "cetta_prime_regular_kernel_admit_closed_conversion_v1("
            if args.mutation == "receipt"
            else (
                "cetta_prime_regular_kernel_admit_closed_synthesis_v1("
                if args.mutation == "synthesis-receipt"
                else "cetta_prime_regular_kernel_admit_closed_checking_v1("
            )
        )
        function_end_name = (
            "cetta_prime_regular_kernel_admitted_conversion_v1_is_current("
            if args.mutation == "receipt"
            else (
                "cetta_prime_regular_kernel_admitted_synthesis_v1_is_current("
                if args.mutation == "synthesis-receipt"
                else "cetta_prime_regular_kernel_admitted_checking_v1_is_current("
            )
        )
        function_start = text.index(function_name)
        function_end = text.index(function_end_name, function_start)
        prefix = text[:function_start]
        body = text[function_start:function_end]
        suffix = text[function_end:]
        body = replace_once(
            body,
            "if (source_binding !=\n"
            "            &prime_typing_open_regular_kernel_source_binding_v1 ||",
            "if (source_binding == NULL ||",
            "source-binding identity",
        )
        text = prefix + body + suffix
    else:
        record = (
            "synthesis" if args.mutation.startswith("synthesis-")
            else "checking" if args.mutation.startswith("checking-")
            else "conversion"
        )
        epoch = "storage" if args.mutation.endswith("generation") else "collection"
        text = replace_once(
            text,
            f"{record}->universe_{epoch}_epoch !=\n"
            f"            live_space->native.universe->{epoch}_epoch ||",
            f"{record}->universe_{epoch}_epoch == UINT64_MAX ||",
            f"{record} {epoch} epoch currentness",
        )
    args.destination.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
