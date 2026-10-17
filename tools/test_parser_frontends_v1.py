#!/usr/bin/env python3
"""Live, saved and baked preparation through authored grammar frontends."""

import argparse
from pathlib import Path

from test_parser_artifacts_v1 import form, headed, lets, run
from parser_artifact_build_v1 import text

ROOT = Path(__file__).resolve().parents[1]


def lbnf_parser(source, name, start):
    return lets([
        ("$read", form("lbnf:read-file", text(source))),
        ("$syntax", form("case", "$read", form(
            form("(LbnfReadValueV1 $binding $source $document)",
                 form("lbnf:lower-document", "$document", text(name), text(start),
                      "(SyntaxLexicalPolicyV1 bnfc-documented)")),
            form("$failure", "$failure")))),
    ], form("langdef:compile-parser", "$syntax", "auto"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/parser-frontends-public")
    parser.add_argument("--module-grammar", type=Path)
    parser.add_argument("--language", choices=("prime", "he", "petta"), action="append")
    args = parser.parse_args()
    binary, output = args.cetta.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    rows = [
        ("bnf-program", form("bnf:parser-from-file", text(ROOT / "langdef/parsers/reusable_v1.bnf"), text("s")),
         "aaaa", "ab"),
        ("ebnf-program", form("case", form("bnf:parser-from-ebnf-file", text(ROOT / "langdef/parsers/reusable_v1.ebnf"), text("s")),
             form(form("(EBNF:SourceParser $parser $binding $source $projection)", "$parser"),
                  form("$failure", "$failure"))),
         "aaaa", "ab"),
        ("token-word", lbnf_parser(ROOT / "langdef/parsers/predictive_word_v1.cf", "token-word", "S"),
         " /* note */ ÿz ", "9"),
    ]
    if args.module_grammar:
        rows.append(("module-parser", lbnf_parser(args.module_grammar.resolve(), "module-parser", "Module"),
                     "Module Example { space NilSpace }", "Module Example {"))
    checks = 0
    for language in args.language or ("prime", "he", "petta"):
        for name, construction, positive, negative in rows:
            path = output / f"{language}-{name}.tpp"
            program = ('!(import! &self "' + str(ROOT / "lib/lib_bnf.metta") + '")\n'
                       '!(import! &self "' + str(ROOT / "lib/lib_lbnf.metta") + '")\n!') + lets([
                ("$live", construction),
                ("$live-value", form("langdef:parser-parse-text", "$live", text(positive))),
                ("$live-evidence", form("langdef:parse-authored-text-evidence", "$live", text(positive), "50000")),
                ("$saved", form("langdef:freeze-parser", "$live", text(path))),
                ("$restored", form("langdef:load-parser-artifact-checked", text(path))),
                ("$baked", form("langdef:load-baked-parser", text(name))),
                ("$restored-value", form("langdef:parser-parse-text", "$restored", text(positive))),
                ("$baked-value", form("langdef:parser-parse-text", "$baked", text(positive))),
                ("$restored-evidence", form("langdef:parse-authored-text-evidence", "$restored", text(positive), "50000")),
                ("$baked-evidence", form("langdef:parse-authored-text-evidence", "$baked", text(positive), "50000")),
                ("$negative", form("langdef:parser-parse-text", "$restored", text(negative))),
                ("$close-live", form("langdef:close-parser", "$live")),
                ("$close-restored", form("langdef:close-parser", "$restored")),
                ("$close-baked", form("langdef:close-parser", "$baked")),
            ], "(function (return (ParserFrontendV1 $live-value $restored-value $baked-value "
               "$live-evidence $restored-evidence $baked-evidence $negative)))") + "\n"
            results = [value for value in run(binary, language, output, name, program)
                       if headed(value, "ParserFrontendV1", 7)]
            if len(results) != 1:
                raise AssertionError((language, name, results))
            result = results[0]
            if not headed(result[1], "ParserAcceptedV1", 2) or result[1] != result[2] or result[1] != result[3]:
                raise AssertionError((language, name, "values/spans/multiplicity", result[1:4]))
            if not headed(result[4], "LangDef:AuthoredParseEvidenceAccepted", 6) or \
                    result[4] != result[5] or result[4] != result[6]:
                raise AssertionError((language, name, "canonical evidence", result[4:7]))
            if not (headed(result[7], "ParserRejectedV1", 3) or headed(result[7], "ParserLexicalRejectedV1", 3)):
                raise AssertionError((language, name, "negative control", result[7]))
            checks += 6
        print(f"PASS: {language} authored frontends, live/restored/baked values and evidence")
    print(f"(ParserFrontendV1Summary {checks} 0)")


if __name__ == "__main__":
    main()
