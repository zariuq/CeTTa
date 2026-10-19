#!/usr/bin/env python3
"""Compare value-only execution with canonical source evidence and diagnostics."""

import argparse
from pathlib import Path
import subprocess

from test_composable_syntax_v1 import form, lets, operand, rule, text
from test_lbnf_values_v1 import headed
from test_parser_artifacts_v1 import run, run_file

ROOT = Path(__file__).resolve().parents[1]
SELECTION = ('(ParserSelectionV1 (LCons (ParserRejectFollowingClassV1 '
             '"re-src:backref" "RxDigitClass") LNil))')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/regex-frontend-equivalence-v1")
    parser.add_argument("--authority-mutator", type=Path,
                        help="exercise a re-signed LR/GLL disagreement without granting execution authority")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    patterns = ["", "a", "a" * 256, "a|ab|a", "(?:" * 64 + "λ" + ")" * 64,
                "(a?)*", "(?<word>[a-z]+)", "[a--b]", "(a)(b)\\12", "(?<=([ab]+)([bc]+))$",
                "|".join(f"keyword{i:05d}" for i in range(256)), "(", "[", "a{", "a\\"]
    checks = 0
    for host in ("he", "petta", "petta-1.1", "prime"):
        program = '!(import! &self langdef)\n'
        for index, pattern in enumerate(patterns):
            program += "!" + lets([
                ("$reader", '(langdef:load-baked-parser "regex-pattern-reader")'),
                ("$fast", form("langdef:parser-parse-text-selected", "$reader", text(pattern), SELECTION)),
                ("$full", form("langdef:parser-parse-text-forest-selected", "$reader", text(pattern), SELECTION, "0")),
                ("$closed", '(langdef:close-parser $reader)'),
            ], form("function", form("return", form("RegexFrontendEquivalenceV1", str(index), "$fast", "$full")))) + "\n"
        rows = [value for value in run(args.cetta.resolve(), host, args.output, "equivalence", program)
                if headed(value, "RegexFrontendEquivalenceV1", 3)]
        assert len(rows) == len(patterns), (host, "missing frontend observations", len(rows))
        for index, row in enumerate(rows):
            fast, full = row[2:]
            assert headed(full, "ParserWithForestV1", 2), (host, index, full)
            if headed(fast, "ParserAcceptedV1", 2):
                assert fast == full[1], (host, index, "selected CST/provenance/multiplicity changed")
            else:
                assert fast == full, (host, index, "diagnostic fallback evidence changed")
            checks += 1
        observations = run_file(args.cetta.resolve(), host, args.output, "example", ROOT / "examples/regex_frontend_v1.metta")
        assert len(observations) == 3 and headed(observations[0], "re:match", 3) and observations[0][1:3] == (0, 5), (host, observations)
        assert headed(observations[1], "re:match", 3) and observations[1][1:3] == (0, 13)
        assert headed(observations[2], "re:source-observation", 1) and headed(observations[2][1], "ParserWithForestV1", 2)
        checks += 3
        if args.authority_mutator:
            original = args.output / (host + "-corridor-authority.tpp")
            mutated = args.output / (host + "-corridor-authority-mutated.tpp")
            publication = '!(import! &self langdef)\n!' + lets([
                ("$syntax", operand("CorridorAuthority", [rule("First", "z"), rule("Second", "z")])),
                ("$parser", form("langdef:compile-parser", "$syntax", "general")),
                ("$published", form("langdef:freeze-parser", "$parser", text(str(original)))),
                ("$closed", form("langdef:close-parser", "$parser")),
            ], '(function (return $published))') + '\n'
            assert any(headed(value, "ParserArtifactV1", 2) for value in
                run(args.cetta.resolve(), host, args.output, "publish-authority", publication))
            subprocess.run([str(args.authority_mutator.resolve()), str(original), str(mutated)], check=True,
                           capture_output=True, text=True, cwd=ROOT)
            control = '!(import! &self langdef)\n!' + lets([
                ("$parser", form("langdef:load-parser-artifact", text(str(mutated)))),
                ("$fast", form("langdef:parser-parse-text-selected", "$parser", text("z"), '(ParserSelectionV1 LNil)')),
                ("$full", form("langdef:parser-parse-text-forest-selected", "$parser", text("z"), '(ParserSelectionV1 LNil)', "0")),
                ("$closed", form("langdef:close-parser", "$parser")),
            ], '(function (return (CorridorAuthorityControlV1 $fast $full)))') + '\n'
            rows = [value for value in run(args.cetta.resolve(), host, args.output, "authority", control)
                    if headed(value, "CorridorAuthorityControlV1", 2)]
            assert len(rows) == 1 and headed(rows[0][1], "ParserAcceptedV1", 2) and len(rows[0][1][1]) == 2
            assert headed(rows[0][2], "ParserWithForestV1", 2) and rows[0][1] == rows[0][2][1], (
                host, "a re-signed LR choice cannot grant a different GLL observer", rows)
            checks += 2
    print(f"(RegexFrontendEquivalenceV1Summary {checks} 0)")


if __name__ == "__main__":
    main()
