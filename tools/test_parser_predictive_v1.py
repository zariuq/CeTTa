#!/usr/bin/env python3
"""Qualify explicit strategies, saved decisions and general evidence."""

import argparse
from pathlib import Path

from gslt2parse_schema_v1 import Symbol
from test_composable_syntax_v1 import form, lets, operand, origin, rule, text
from test_lbnf_values_v1 import CFLEX, library_import, lower_read
from test_parser_artifacts_v1 import headed, run

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/parser-predictive-public")
    args = parser.parse_args()
    binary, output = args.cetta.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = [
        ("scalar", operand("PredictiveScalar", [rule("Same", "a")]), "a"),
        ("token", lower_read('Word. S ::= Ident; comment "/*" "*/";', name="PredictiveToken", start=text("S")), " /* note */ ÿz "),
        ("c-flex", lower_read('StringValue. S ::= String;', name="PredictiveCFlex", start=text("S"), policy=CFLEX), ' "x\\n" '),
        ("joined", lets([
            ("$left", operand("Left", [rule("Left", "a")], sites=[origin("left", 0)])),
            ("$right", operand("Right", [rule("Right", "b")], sites=[origin("right", 0)])),
        ], form("langdef:join-syntax-gslts", text("PredictiveJoin"), text("E"), "$left", "$right")), "b"),
    ]
    count = 0
    for language in ("prime", "he", "petta"):
        for name, syntax, sample in cases:
            path = output / f"{language}-{name}.tpp"
            program = library_import() + "\n".join([
                "(= (ParserInfoV1 $n $s $e $u $p $w) corrupted)",
                "(= (ParserAcceptedV1 $v $p) corrupted)",
                "(= (Same) corrupted)",
            ]) + "\n!" + lets([
                ("$syntax", syntax),
                ("$general", form("langdef:compile-parser", "$syntax", "general")),
                ("$predictive", form("langdef:compile-parser", "$syntax", "predictive")),
                ("$auto", form("langdef:compile-parser", "$syntax", "auto")),
                ("$general-info", form("langdef:parser-info", "$general")),
                ("$predictive-info", form("langdef:parser-info", "$predictive")),
                ("$auto-info", form("langdef:parser-info", "$auto")),
                ("$general-value", form("langdef:parser-parse-text", "$general", text(sample))),
                ("$predictive-value", form("langdef:parser-parse-text", "$predictive", text(sample))),
                ("$auto-value", form("langdef:parser-parse-text", "$auto", text(sample))),
                ("$general-evidence", form("langdef:parse-authored-text-evidence", "$general", text(sample), "10000")),
                ("$predictive-evidence", form("langdef:parse-authored-text-evidence", "$predictive", text(sample), "10000")),
                ("$frozen", form("langdef:freeze-parser", "$auto", text(str(path)))),
                ("$close-auto", form("langdef:close-parser", "$auto")),
                ("$restored", form("langdef:load-parser-artifact", text(str(path)))),
                ("$checked", form("langdef:load-parser-artifact-checked", text(str(path)))),
                ("$restored-info", form("langdef:parser-info", "$restored")),
                ("$restored-value", form("langdef:parser-parse-text", "$restored", text(sample))),
                ("$checked-value", form("langdef:parser-parse-text", "$checked", text(sample))),
                ("$reject", form("langdef:parser-parse-text", "$predictive", text("?"))),
                ("$close-general", form("langdef:close-parser", "$general")),
                ("$surviving", form("langdef:parser-parse-text", "$predictive", text(sample))),
                ("$close-predictive", form("langdef:close-parser", "$predictive")),
                ("$close-restored", form("langdef:close-parser", "$restored")),
                ("$close-checked", form("langdef:close-parser", "$checked")),
            ], "(function (return (PredictivePublicControlV1 $general-info $predictive-info $auto-info "
               "$general-value $predictive-value $auto-value $general-evidence $predictive-evidence "
               "$frozen $restored-info $restored-value $checked-value $reject $surviving "
               "$close-general $close-predictive $close-restored $close-checked)))") + "\n"
            values = run(binary, language, output, name, program)
            records = [v for v in values if headed(v, "PredictivePublicControlV1", 18)]
            if len(records) != 1:
                raise AssertionError((language, name, values))
            row = records[0]
            for index, strategy, engine in ((1, "general", "gll"), (2, "predictive", "predictive"),
                                            (3, "auto", "predictive"), (10, "auto", "predictive")):
                if not headed(row[index], "ParserInfoV1", 6) or row[index][2:4] != (Symbol(strategy), Symbol(engine)) or \
                        row[index][6] != Symbol("ParserPredictiveEligibleV1"):
                    raise AssertionError((language, name, "strategy selection", row[index]))
            if row[1][4] != 3 or row[2][4] != 3 or row[3][4] != 3:
                raise AssertionError((language, name, "strategies must share one immutable owner", row[1:4]))
            if not headed(row[4], "ParserAcceptedV1", 2) or any(row[i] != row[4] for i in (5, 6, 11, 12, 14)):
                raise AssertionError((language, name, "values or spans changed", row))
            if row[7] != row[8] or not headed(row[9], "ParserArtifactV1", 2):
                raise AssertionError((language, name, "evidence or freeze", row[7:10]))
            if not (headed(row[13], "ParserRejectedV1", 3) or headed(row[13], "ParserLexicalRejectedV1", 3)):
                raise AssertionError((language, name, "predictive rejection is not a typed rejection", row[13]))
            if any(v not in (Symbol("True"), Symbol("true")) for v in row[15:]):
                raise AssertionError((language, name, "close ordering", row[15:]))
            count += 14

        ambiguous = lower_read('First. S ::= "a"; Second. S ::= "a";', name="AmbiguousAuto", start=text("S"))
        negative = library_import() + "!" + lets([
            ("$syntax", ambiguous),
            ("$auto", form("langdef:compile-parser", "$syntax", "auto")),
            ("$info", form("langdef:parser-info", "$auto")),
            ("$forced", form("langdef:compile-parser", "$syntax", "predictive")),
            ("$value", form("langdef:parser-parse-text", "$auto", text("a"))),
            ("$closed", form("langdef:close-parser", "$auto")),
        ], "(function (return (PredictiveNegativeV1 $info $forced $value $closed)))") + "\n"
        rows = [v for v in run(binary, language, output, "ambiguous", negative) if headed(v, "PredictiveNegativeV1", 4)]
        if len(rows) != 1 or not headed(rows[0][1], "ParserInfoV1", 6) or rows[0][1][2:4] != (Symbol("auto"), Symbol("gll")) or \
                not headed(rows[0][1][6], "ParserSelectConflictV1", 6) or \
                not headed(rows[0][2], "ParserPredictiveRefusedV1", 1) or rows[0][2][1] != rows[0][1][6] or \
                not headed(rows[0][3], "ParserAcceptedV1", 2) or len(rows[0][3][1]) != 2:
            raise AssertionError((language, "ambiguous general retention and positioned conflict", rows))
        count += 5
        print(f"PASS: {language} explicit strategies, predictive restoration and complete general evidence")
    print(f"(ParserPredictivePublicV1Summary {count} 0)")


if __name__ == "__main__":
    main()
