#!/usr/bin/env python3
"""Deep parsed module terms across the public data boundaries."""
import argparse
from pathlib import Path

from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed
from test_parser_artifacts_v1 import run
from test_module_values_v1 import reader_load

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--reader-artifact", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--depth", type=int, default=10000)
    parser.add_argument("--language", choices=("he", "petta", "prime"), action="append")
    args = parser.parse_args()
    if args.depth < 1:
        parser.error("depth must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = output / "nested.txt"
    source.write_text("s(" * args.depth + "z" + ")" * args.depth)
    count = 0
    for language in args.language or ("he", "petta", "prime"):
        bindings = [
            ("$reader", reader_load(args.reader_artifact.resolve() if args.reader_artifact else None)),
            ("$module", form("module:read-file", "$reader", text("deep-naturals"),
                             text(str(ROOT / "tests/module_values/RewriteBundles.module")))),
            ("$reader-close", form("langdef:close-parser", "$reader")),
            ("$presentation", form("module:assemble", "$module", text("Naturals"), "LNil", "LNil",
                                   "OriginViewV1", text("deep-naturals:instance"))),
            ("$language", form("module:language", "$presentation", text("DeepNaturals"), text("Nat"),
                               "(SyntaxLexicalPolicyV1 bnfc-documented)")),
            ("$core", form("module:language-core", "$language")),
            ("$syntax", form("module:language-syntax", "$language")),
            ("$parser", form("langdef:compile-parser", "$syntax", "auto")),
            ("$authority", form("langdef:load-language-value", "$core")),
            ("$parsed", form("langdef:parser-parse-file", "$parser", text(str(source)))),
            ("$projected", form("langdef:project-parser-result", "$authority", text("Nat"), "$parsed")),
            ("$term", form("langdef:projected-term", "$projected", "0")),
            ("$admitted", form("langdef:admit-term-data", "$authority", text("Nat"), "$term")),
            ("$parser-close", form("langdef:close-parser", "$parser")),
            ("$authority-close", form("langdef:close-language", "$authority")),
        ]
        program = ('!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n'
                   '(= (NatS $value) corrupted)\n(= (LbnfAstNodeV1 $c $l $s $v) corrupted)\n!' +
                   lets(bindings, "(function (return (ModuleDeepProjectionV1 $admitted)))") + "\n")
        records = [v for v in run(args.cetta.resolve(), language, output, "deep-projection", program)
                   if headed(v, "ModuleDeepProjectionV1", 1)]
        if len(records) != 1 or not headed(records[0][1], "LangDef:TermAdmitted", 0):
            raise AssertionError((language, "deep parsed term did not reach admission", records))
        count += 1
        print(f"PASS: {language} {args.depth}-level parse, projection, inert extraction and admission", flush=True)
    print(f"(ModuleDeepProjectionV1Summary {count} 0)")


if __name__ == "__main__":
    main()
