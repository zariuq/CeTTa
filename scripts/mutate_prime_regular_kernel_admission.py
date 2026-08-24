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
    elif args.mutation == "generation":
        text = replace_once(
            text,
            "conversion->universe_storage_epoch !=\n"
            "            live_space->native.universe->storage_epoch ||",
            "conversion->universe_storage_epoch == UINT64_MAX ||",
            "storage-generation currentness",
        )
    elif args.mutation == "synthesis-generation":
        text = replace_once(
            text,
            "synthesis->universe_storage_epoch !=\n"
            "            live_space->native.universe->storage_epoch ||",
            "synthesis->universe_storage_epoch == UINT64_MAX ||",
            "synthesis storage-generation currentness",
        )
    else:
        text = replace_once(
            text,
            "checking->universe_storage_epoch !=\n"
            "            live_space->native.universe->storage_epoch ||",
            "checking->universe_storage_epoch == UINT64_MAX ||",
            "checking storage-generation currentness",
        )
    args.destination.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
