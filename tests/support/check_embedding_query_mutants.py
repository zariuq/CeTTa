#!/usr/bin/env python3
"""Discriminate retained-query fuel, work, owner and binding defects."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("cc", "cppflags", "cflags", "ldflags"):
        parser.add_argument("--" + option, required=True)
    parser.add_argument("--objects", nargs="+", required=True)
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    flags = shlex.split(args.cppflags) + shlex.split(args.cflags) + ["-Iinclude"]
    # Fault controls test semantics, so compile only the substituted object
    # without optimization; all other objects retain the qualified cohort.
    flags = [flag for flag in flags if not flag.startswith("-O")] + ["-O0"]
    link = shlex.split(args.ldflags)
    cases = [
        ("source-purse", "src/eval_query.inc", "g_eval_completion_tracker = &query->episode;",
         "query->episode = cetta_eval_episode_begin(NULL, query->fuel);\n"
         "    g_eval_completion_tracker = &query->episode;", "unchanged == spent"),
        ("physical-allowance", "src/eval_query.inc", "work->budget = allowance;",
         "work->budget = UINT64_MAX;", "physical <= allowance"),
        ("owner-root", "src/eval.c",
         "scope->pair = (CettaEvalCancellationPair){.owner = token};",
         "scope->pair = (CettaEvalCancellationPair){.owner = NULL};",
         "eval_native_interrupted()"),
        ("binding-owner", "src/eval_query.inc",
         "packet->bindings = bindings_capture_value(arena, returned->environment);",
         "packet->bindings = atom_unit(arena);", "bindings_restore_captured_value"),
        ("he-read-ahead", "src/eval_he_canonical.inc",
         "if (driver->retained) {", "if (false && driver->retained) {", "result->count == 1"),
        ("he-provisional-error", "src/eval_he_canonical.inc",
         "if (normalize && (empty || error)) continue;",
         "if (normalize && empty) continue;", "found < ordinary->count"),
        ("he-function-read-ahead", "src/eval_he_boundary.inc",
         "while (frame && frame->phase == HE_INSTRUCTION_FUNCTION) {",
         "while (false && frame && frame->phase == HE_INSTRUCTION_FUNCTION) {", "result->count == 1u"),
        ("he-captured-options", "src/eval_he_retained.inc",
         "*active_eval_options() = *outer;", "(void)outer;",
         "options.max_stack_depth == outside"),
        ("he-return-binding", "src/eval_he_canonical.inc",
         "frame->preserve_bindings ? &frame->child.items[i].env : &empty",
         "&empty", "bindings.len == 1"),
        ("he-delayed-packet", "src/eval_he_canonical.inc",
         "outcome_set_delay_last(frame->target, frame->child.items[i].delayed);",
         "outcome_set_delay_last(frame->target, NULL);",
         "packet.kind == CETTA_CALL_SUSPENDED && packet.delayed"),
        ("callback-he-read-ahead", "src/eval_he_canonical.inc",
         "if (he_retained_execution_active() && cetta_foreign_provider_callable(head)) {",
         "if (false && he_retained_execution_active() && cetta_foreign_provider_callable(head)) {",
         "stream.opens == 1u && stream.pulls == 1u"),
        ("callback-raised-is-cancelled", "src/eval.c",
         "if (eval_cancel_check()) completion = CETTA_EVAL_INCOMPLETE_CANCELLED;",
         "if (eval_native_interrupted()) completion = CETTA_EVAL_INCOMPLETE_CANCELLED;",
         "caught_status =="),
        ("petta-root-delimiter", "src/eval.c",
         "if (head == g_builtin_syms.function && expression->expr.len == 2u)",
         "if (false && head == g_builtin_syms.function && expression->expr.len == 2u)",
         "found < ordinary->count"),
        ("retention-output", "src/eval_he_retained.inc",
         "outcome_set_clear(&work->output);\n            work->consumed = 0u;",
         "/* Keep the consumed output slots. */", "slots <= 64u"),
        ("retention-relay", "src/eval_he_canonical.inc",
         "outcome_set_clear(source);\n        frontier->seen = 0u;",
         "/* Keep the forwarded relay slots. */", "slots <= 64u"),
        ("retention-raw-return", "src/eval_he_canonical.inc",
         ".discard_drained = true", ".discard_drained = false", "slots <= 64u"),
        ("retention-function-return", "src/eval_he_boundary.inc",
         "if (frame->index == frame->inner.len) {",
         "if (false && frame->index == frame->inner.len) {", "slots <= 64u"),
        ("retention-resource-classification", "src/eval_he_canonical.inc",
         "if (!frame) {\n        eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_CAPACITY);\n        return NULL;",
         "if (!frame) {\n        return NULL;", "step == CETTA_QUERY_STOPPED"),
        ("he-function-delay", "src/eval_he_boundary.inc",
         "outcome_set_delay_last(frame->target, source->delayed);",
         "outcome_set_delay_last(frame->target, NULL);",
         "packet.kind == CETTA_CALL_SUSPENDED && packet.delayed"),
        ("callback-function-blocks-nonreturn", "src/eval_he_boundary.inc",
         "if (!result || !he_instruction_complete_function_return(frame, item, result))",
         "if (!result || atom_head_symbol_id(result) != g_builtin_syms.return_text || !he_instruction_complete_function_return(frame, item, result))",
         "integer(value) == 11 && mixed.pulls == 2u"),
    ]
    baseline = Path("src/eval.c").read_text()
    with tempfile.TemporaryDirectory(prefix="embedding-query-mutants-", dir="runtime") as directory:
        root = Path(directory)
        for name, source, before, after, discriminator in cases:
            body = Path(source).read_text()
            if body.count(before) != 1:
                raise RuntimeError(f"{name}: expected exactly one source anchor")
            changed = body.replace(before, after)
            if source != "src/eval.c":
                anchor = '#include "' + Path(source).name + '"'
                if baseline.count(anchor) != 1:
                    raise RuntimeError(f"{name}: expected one include")
                changed = baseline.replace(anchor, changed)
            variant, obj, binary = root / f"{name}.c", root / f"{name}.o", root / name
            variant.write_text(changed)
            retirement = name.startswith("retention-")
            if retirement:
                control = Path("tests/support/test_he_retained_execution.c").read_text()
                variant.write_text(control.replace('#include "../../src/eval.c"', changed))
            subprocess.run(compiler + flags + ["-c", str(variant), "-o", str(obj)], check=True)
            objects = [path for path in args.objects if not Path(path).name.startswith("eval.")]
            if len(objects) != len(args.objects) - 1:
                raise RuntimeError(f"{name}: expected one evaluator object")
            callback = name.startswith("callback-")
            consumer = [] if retirement else ["tests/test_embedding_callbacks.c", "src/embedding.c"] if callback else ["tests/support/test_embedding_query.c"]
            subprocess.run(compiler + flags + [*consumer, str(obj),
                           *objects, "-o", str(binary), *link], check=True)
            result = subprocess.run([str(binary), "--he" if callback else "--require-streaming"],
                                    capture_output=True, text=True, timeout=20)
            if result.returncode == 0 or discriminator not in result.stderr:
                raise RuntimeError(f"{name}: missed intended assertion\n{result.stderr}")
            print(f"PASS: {name} rejected at {discriminator}", flush=True)
    print(f"Typed query negative controls: {len(cases)} rejected")


if __name__ == "__main__":
    main()
