#!/usr/bin/env python3

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one mutation boundary, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mutation", required=True, choices=(
        "native-check-disabled", "undetermined-accepted",
        "solved-slot-disabled", "result-executed"))
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    text = args.source.read_text(encoding="utf-8")

    if args.mutation == "result-executed":
        start = text.index("static void main_add_he_prime_typing_op_decls(")
        end = text.index("static void main_add_prime_semantic_op_decls(", start)
        body = replace_once(text[start:end],
                            "elems[n + 1] = atom_atom_type(arena);",
                            "elems[n + 1] = atom_undefined_type(arena);")
    elif args.mutation == "solved-slot-disabled":
        start = text.index("static bool chain_schedule_next_premise(")
        end = text.index("static bool chain_finish_continuation(", start)
        body = replace_once(text[start:end],
                            "if (argument && domain && !atom_has_vars(argument) &&",
                            "if (false && argument && domain && !atom_has_vars(argument) &&")
    else:
        start = text.index("static bool chain_proof_checked(")
        end = text.index("static bool chain_guidance_premise_supported(", start)
        body = text[start:end]
        if args.mutation == "native-check-disabled":
            body = replace_once(body, "if (ctx->prime) {",
                                "if (false && ctx->prime) {")
        else:
            body = replace_once(body,
                "if (outcome != CETTA_NIK_OUTCOME_ESTABLISHED ||\n"
                "            !observation.authority.canonical_term) {",
                "if (outcome == CETTA_NIK_OUTCOME_INCOMPLETE) {")
    args.destination.write_text(text[:start] + body + text[end:],
                                encoding="utf-8")


if __name__ == "__main__":
    main()
