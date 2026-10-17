#!/usr/bin/env python3
"""Deep reusable parser values through all three public language boundaries."""

import argparse
from pathlib import Path
import subprocess

from test_composable_syntax_v1 import form, lets, operand, rule, text

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/parser-deep-public")
    args = parser.parse_args()
    binary, output = args.cetta.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    depth = 50000
    source = output / "nested.txt"
    source.write_text("a" * depth + "b" * depth)
    syntax = operand("DeepParser", [
        rule("Wrap", "", parameters=[("child", "E")],
             syntax=[form("SyntaxTerminal", text("a")), form("SyntaxNonTerminal", text("child")),
                     form("SyntaxTerminal", text("b"))]),
        rule("Empty", "", syntax=[]),
    ])
    count = 0
    for language in ("prime", "he", "petta"):
        path = output / f"{language}.tpp"
        body = lets([
            ("$syntax", syntax), ("$live", form("langdef:compile-parser", "$syntax", "auto")),
            ("$info", form("langdef:parser-info", "$live")),
            ("$first", form("langdef:parser-parse-file", "$live", text(str(source)))),
            ("$frozen", form("langdef:freeze-parser", "$live", text(str(path)))),
            ("$closed", form("langdef:close-parser", "$live")),
            ("$restored", form("langdef:load-parser-artifact-checked", text(str(path)))),
            ("$again", form("langdef:parser-parse-file", "$restored", text(str(source)))),
            ("$same", "(== $first $again)"),
            ("$last-close", form("langdef:close-parser", "$restored")),
        ], form("case", "$first", form(
            form(form("ParserAcceptedV1", "$values", form("ParserSpanV1", "0", str(depth * 2), "0", str(depth * 2))),
                 "(function (return (DeepParserPublicV1 $info $same $closed $last-close)))"),
            form("$failure", "(function (return (DeepParserFailureV1 $failure)))"))))
        program = output / f"{language}.metta"
        program.write_text("!(import! &self langdef)\n(= (Wrap $child) corrupted)\n"
                           "(= (CstRuleV1 $label $begin $end $child) corrupted)\n!" + body + "\n")
        command = [str(binary), "--lang", language]
        if language != "prime":
            command.extend(["--profile", "extended"])
        result = subprocess.run(command + [str(program)], cwd=ROOT, text=True,
                                capture_output=True, timeout=300)
        (output / f"{language}.stdout").write_text(result.stdout)
        (output / f"{language}.stderr").write_text(result.stderr)
        if result.returncode or "DeepParserPublicV1" not in result.stdout or \
                "auto predictive" not in result.stdout or "corrupted" in result.stdout or \
                "DeepParserFailureV1" in result.stdout or "False" in result.stdout or "false" in result.stdout:
            raise AssertionError((language, result.returncode, result.stdout[:500], result.stderr[-500:]))
        count += 4
        print(f"PASS: {language} 50000-level live/restored values and sealed constructors")
    print(f"(ParserDeepPublicV1Summary {count} 0)")


if __name__ == "__main__":
    main()
