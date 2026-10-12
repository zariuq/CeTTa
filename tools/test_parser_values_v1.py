#!/usr/bin/env python3
"""Qualify lightweight parser values and independent shared handles."""

import argparse
from pathlib import Path
import subprocess

from test_composable_syntax_v1 import form, lets, parts, rule, text

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/parser-values-public")
    args = parser.parse_args()
    binary = args.cetta.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    language, profile, _, _ = parts("reused", [rule("Same", "a")])
    load = form("langdef:load-authored-parser-values", language, profile)
    body = lets([
        ("$first", load), ("$second", load),
        ("$info", form("langdef:parser-info", "$first")),
        ("$first-value", form("langdef:parser-parse-text", "$first", text("a"))),
        ("$closed", form("langdef:close-parser", "$first")),
        ("$second-value", form("langdef:parser-parse-text", "$second", text("a"))),
        ("$rejected", form("langdef:parser-parse-text", "$second", text("b"))),
        ("$closed-value", form("langdef:parser-parse-text", "$first", text("a"))),
        ("$last-info", form("langdef:parser-info", "$second")),
        ("$last-close", form("langdef:close-parser", "$second")),
    ], "(function (return (ParserValuesPublicV1 $info $first-value $closed $second-value $rejected $closed-value $last-info $last-close)))")
    # Constructors returned by the native must remain data in every profile.
    program = "!(import! &self langdef)\n" + "\n".join([
        "(= (Same) corrupted)",
        "(= (ParserAcceptedV1 $values $span) corrupted)",
        "(= (ParserInfoV1 $name $strategy $engine $owners $productions $witness) corrupted)",
        "!" + body,
    ]) + "\n"
    total = 0
    for language_name, dialect_profile in [("prime", None), ("he", "extended"), ("petta", "extended")]:
        source = args.output / f"{language_name}.metta"
        source.write_text(program)
        command = [str(binary), "--lang", language_name]
        if dialect_profile:
            command += ["--profile", dialect_profile]
        result = subprocess.run(command + [str(source)], cwd=ROOT,
                                capture_output=True, text=True, timeout=120)
        (args.output / f"{language_name}.stdout").write_text(result.stdout)
        (args.output / f"{language_name}.stderr").write_text(result.stderr)
        expected = [
            '(ParserInfoV1 "reused" auto predictive 2 ',
            '(ParserInfoV1 "reused" auto predictive 1 ',
            "ParserAcceptedV1", "ParserSpanV1 0 1 0 1",
            "ParserRejectedV1 0 0", "ParserFailureV1", "Same",
        ]
        if result.returncode or any(item not in result.stdout for item in expected) or "corrupted" in result.stdout:
            raise AssertionError((language_name, result.returncode, result.stdout, result.stderr))
        if result.stdout.count("ParserAcceptedV1") != 2:
            raise AssertionError((language_name, "expected two live-handle parses", result.stdout))
        total += len(expected) + 2
    print(f"(ParserValuesPublicV1Summary {total} 0)")


if __name__ == "__main__":
    main()
