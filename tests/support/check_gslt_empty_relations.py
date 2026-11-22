"""Clause-free goals complete locally; providers and earlier work retain meaning."""

import argparse
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from gslt_relation_executor_v1 import completed
from native_observation import check_petta_fuel_exhaustion


def compile_program(args, sources, target, output, closed=False, epilogue=None):
    command = [str(args.compiler), "petta-direct"]
    for source in sources:
        command += ["--source", str(source)]
    command += ["--target", target]
    for entry in ("UsesMissingV1:10", "UsesNullaryV1:1", "NoFactsV1:", "NoFactsV1:1"):
        command += ["--closed-entry-mode" if closed else "--entry-mode", entry]
    if epilogue is not None:
        command += ["--epilogue", str(epilogue)]
    completed(command + ["--out", str(output)], 30)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    args.compiler = args.compiler.resolve()
    args.runtime = args.runtime.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    base = ROOT / "tests/petta/gslt_empty_relations_v1.metta"
    provider = ROOT / "tests/petta/gslt_empty_provider_v1.metta"
    for supplied in (False, True):
        sources = [base, provider] if supplied else [base]
        first_count, total = (2, 3) if supplied else (0, 0)
        for target in ("petta", "he"):
            for closed in (False, True):
                name = f"{'provided' if supplied else 'empty'}-{target}-{'closed' if closed else 'generic'}"
                program = work / (name + ".metta")
                expressions = [("ModeCountV1", "(gslt:entry:UsesMissingV1:10 anchor)", total)]
                expressions.append(("NullaryModeV1", "(gslt:entry:UsesNullaryV1:1 anchor)", 0))
                expressions.append(("NullaryEntryV1", "(gslt:entry:NoFactsV1:)", 0))
                expressions.append(("OtherArityV1", "(gslt:entry:NoFactsV1:1 present)", 1))
                if not closed:
                    expressions += [
                        ("GenericCountV1", "(gslt:UsesMissingV1 (UsesMissingV1 anchor first))", first_count),
                        ("MissingCountV1", "(gslt:MissingV1 (MissingV1 anchor first))", first_count),
                        ("IndependentV1", "(gslt:PresentV1 (PresentV1 present))", 1),
                        ("NullaryGenericV1", "(gslt:NoFactsV1 (NoFactsV1))", 0),
                    ]
                query = work / (name + ".query.metta")
                query.write_text("".join(
                    f"!(let $bag (collapse {expression}) "
                    f"(let $count (size-atom $bag) "
                    f"(if (== $count {count}) {label} UnexpectedV1)))\n"
                    for label, expression, count in expressions))
                compile_program(args, sources, target, program, closed,
                                epilogue=query if target == "he" else None)
                documents = [str(program)] if target == "he" else [str(program), str(query)]
                actual = completed([sys.executable, str(ROOT / "tests/support/run_gslt_program.py"),
                                    str(args.runtime), "--lang", target, *documents], 30)
                expected = "\n".join(f"[{label}]" if target == "he" else label
                                     for label, _, _ in expressions)
                assert actual == expected, (name, expected, actual)

    generated = work / "empty.c"
    compile_program(args, [base], "c", generated, closed=True)
    native = work / "empty-native"
    completed([*shlex.split(args.cc), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I.",
               f'-DGSLT_C_PROGRAM="{generated}"', "tests/support/gslt_c_answers_v1.c", "-o", str(native)], 30)
    calls = work / "empty.calls"
    calls.write_text("UsesMissingV1:10 present\nUsesNullaryV1:1 present\nNoFactsV1:\nNoFactsV1:1 present\n")
    run = subprocess.run([str(native), "--calls", str(calls)], capture_output=True, text=True, timeout=30, check=True)
    assert run.stdout == "()\n()\n()\n((gslt:result:NoFactsV1:1))\n", run.stdout

    # An earlier nonterminating interpretation must still be pending: an
    # empty later goal does not license dropping the preceding computation.
    prefix = work / "prefix.metta"
    completed([str(args.compiler), "petta-direct", "--source",
               str(ROOT / "tests/petta/gslt_empty_prefix_v1.metta"),
               "--entry-mode", "UsesLoopV1:10", "--out", str(prefix)], 30)
    for entry, query in enumerate(("(gslt:UsesLoopV1 (UsesLoopV1 anchor $answer))",
                                   "(gslt:entry:UsesLoopV1:10 anchor)")):
        document = work / f"prefix-{entry}.metta"
        document.write_text(f"!{query}\n")
        check_petta_fuel_exhaustion(args.runtime, [prefix, document],
                                    work / f"prefix-{entry}.events.jsonl")
    print("PASS: clause-free relations, supplied duplicate occurrences and closed entries agree on PeTTa and HE")
    print("PASS: the C target's empty entry builds with strict warnings and returns an empty bag")
    print("PASS: an earlier unfinished interpretation is not erased by a later empty relation")


if __name__ == "__main__":
    main()
