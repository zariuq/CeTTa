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
        start = text.index("static void emit_completed_native_value(")
        end = text.index("static void emit_completed_native_call(", start)
        boundary = "    if (eval_current_language_id() == CETTA_LANGUAGE_HE && expected_type &&"
        activate = """    static _Thread_local bool executing_result;
    if (!executing_result && eval_current_language_id() == CETTA_LANGUAGE_PRIME && value &&
        value->kind == ATOM_EXPR && atom_authored_len(value) == 2u &&
        atom_is_symbol(value->expr.elems[0], "he-accept") &&
        value->expr.elems[1]->kind == ATOM_EXPR &&
        value->expr.elems[1]->expr.len > 0u &&
        atom_is_symbol(value->expr.elems[1]->expr.elems[0], "typed-answer-v2")) {
        Atom *activation = atom_expr2(a, atom_symbol_id(a, g_builtin_syms.eval), value);
        executing_result = true;
        if (activation)
            metta_call_with_env(s, a, activation, NULL, -1, environment, outcomes);
        executing_result = false;
        return;
    }
"""
        body = replace_once(text[start:end], boundary, activate + boundary)
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
