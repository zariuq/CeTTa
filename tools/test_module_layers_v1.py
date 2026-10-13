#!/usr/bin/env python3
"""Qualify independent module-authored declaration layers over one core."""
import argparse
from pathlib import Path
import subprocess

from test_parser_artifacts_v1 import run
from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed, output_values
from gslt2parse_schema_v1 import Symbol
from test_module_values_v1 import rows, reader_load

ROOT = Path(__file__).resolve().parents[1]


def handoff_example(binary, output, language):
    command = [str(binary), "--lang", language]
    if language != "prime":
        command += ["--profile", "extended"]
    command.append(str(ROOT / "examples/module_rewrite_values_v1.metta"))
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
    (output / f"{language}-handoff.stdout").write_text(completed.stdout)
    (output / f"{language}-handoff.stderr").write_text(completed.stderr)
    if completed.returncode:
        raise AssertionError((language, "handoff example execution", completed.stderr))
    records = [v for v in output_values(completed.stdout) if headed(v, "ModuleWorkflowExampleV1", 5)]
    assert len(records) == 1, (language, records)
    extended, term, admission, steps, rules = records[0][1:]
    assert headed(extended, "ModuleExtendedLanguageV1", 2), extended
    assert len(rows(extended[2])) == 2, extended
    assert term == (Symbol("NatAdd"), (Symbol("NatZ"),), (Symbol("NatS"), (Symbol("NatZ"),))), term
    assert admission == (Symbol("LangDef:TermAdmitted"),), admission
    assert headed(steps, "LangDef:ContextualTermResults", 2), steps
    assert steps[1] == Symbol("LangDef:ContextComplete"), steps
    assert steps[2][1] == (Symbol("NatS"), (Symbol("NatZ"),)), steps
    assert headed(rules, "ModuleRewriteRulesV1", 2) and len(rows(rules[2])) == 2, rules
    print(f"ModuleHandoffExampleSummary {language} 1 0")


def controls(binary, reader, output, language):
    bindings = [("$reader", reader_load(reader)),
                ("$base-module", form("module:read-file", "$reader", text("base"),
                   text(str(ROOT / "tests/module_values/RewriteBundles.module")))),
                ("$naturals", form("module:assemble", "$base-module", text("Naturals"),
                   "LNil", "LNil", "OriginViewV1", text("base-instance"))),
                ("$base-presentation", form("module:assemble", "$base-module", text("Arithmetic"),
                   "(LCons $naturals LNil)", "LNil", "OriginViewV1", text("arithmetic-instance"))),
                ("$base", form("module:core", "$base-presentation", text("Base")))]
    for stem, theory, category, sample, tag in (
        ("SourceNotes", "Notes", "Note", "proof-source", "notes"),
        ("SearchHints", "Hints", "Hint", "depth-first", "hints"),
    ):
        bindings += [
            ("$" + tag + "-module", form("module:read-file", "$reader", text(stem),
                text(str(ROOT / "tests/module_values" / (stem + ".module"))))),
            ("$" + tag + "-presentation", form("module:assemble", "$" + tag + "-module", text(theory),
                "LNil", "LNil", "OriginViewV1", text(stem + ":instance"))),
            ("$" + tag + "-language", form("module:language", "$" + tag + "-presentation", text(stem),
                text(category), "(SyntaxLexicalPolicyV1 bnfc-documented)")),
            ("$" + tag + "-core", form("module:language-core", "$" + tag + "-language")),
            ("$" + tag + "-syntax", form("module:language-syntax", "$" + tag + "-language")),
            ("$" + tag + "-parser", form("langdef:compile-parser", "$" + tag + "-syntax", "auto")),
            ("$" + tag + "-parse", form("langdef:parser-parse-text", "$" + tag + "-parser", text(sample))),
            ("$" + tag + "-authority", form("langdef:load-language-value", "$" + tag + "-core")),
            ("$" + tag + "-projection", form("langdef:project-parser-result", "$" + tag + "-authority",
                text(category), "$" + tag + "-parse")),
            ("$" + tag + "-field", form("langdef:projected-term", "$" + tag + "-projection", "0")),
            ("$" + tag, form("module:attach-layer", "$base", f'(SyntaxOriginV1 "{stem}" "layer")',
                "$" + tag + "-core", text(category), "(LCons $" + tag + "-field LNil)")),
            ("$" + tag + "-close-parser", form("langdef:close-parser", "$" + tag + "-parser")),
            ("$" + tag + "-close-authority", form("langdef:close-language", "$" + tag + "-authority")),
        ]
    bindings += [
        ("$both", form("module:join-layers", "$notes", "$hints")),
        ("$both-again", form("module:join-layers", "$both", "$notes")),
        ("$over-same", form("module:join-layers-over", "$base", "$notes", "$hints")),
        ("$foreign-notes", form("module:join-layers-over", "$notes-core", "$notes", "$notes-core")),
        ("$over-different", form("module:join-layers-over", "$base", "$foreign-notes", "$hints")),
        ("$over-malformed", form("module:join-layers-over", "BadCore", "$notes", "$hints")),
        ("$over-malformed-wire", form("module:join-layers-over",
            '(GSLTLanguageDefWireV1 "bad" not-a-list LNil LNil LNil)', "$notes", "$hints")),
        ("$core", form("module:base-core", "$both")),
        ("$note-fields", form("module:layer-fields", "$both", '(SyntaxOriginV1 "SourceNotes" "layer")')),
        ("$hint-fields", form("module:layer-fields", "$both", '(SyntaxOriginV1 "SearchHints" "layer")')),
        ("$wrong-field", form("module:attach-layer", "$base", '(SyntaxOriginV1 "SourceNotes" "bad")',
            "$notes-core", text("Note"), "(LCons (SearchDepth) LNil)")),
        ("$changed-note", form("module:attach-layer", "$base", '(SyntaxOriginV1 "SourceNotes" "layer")',
            "$notes-core", text("Note"), '(LCons (UserNote) LNil)')),
        ("$conflict", form("module:join-layers", "$notes", "$changed-note")),
        ("$over-conflict", form("module:join-layers-over", "$notes-core", "$notes", "$changed-note")),
        ("$mismatch", form("module:join-layers", "$notes", "$notes-core")),
        ("$missing", form("module:layer-fields", "$both", '(SyntaxOriginV1 "missing" "layer")')),
        ("$duplicate-fields", form("module:attach-layer", "$base", '(SyntaxOriginV1 "SourceNotes" "twice")',
            "$notes-core", text("Note"), "(LCons $notes-field (LCons $notes-field LNil))")),
        ("$laws", form("module:assemble", "$base-module", text("Arithmetic"),
            "(LCons $naturals LNil)", "LNil", "OriginViewV1", text("laws"))),
        ("$law-core", form("module:core", "$laws", text("Laws"))),
        ("$law-layer", form("module:attach-layer", "$base", '(SyntaxOriginV1 "laws" "layer")',
            "$law-core", text("Nat"), "(LCons (NatZ) LNil)")),
        ("$empty-unknown", form("module:attach-layer", "$base", '(SyntaxOriginV1 "empty" "layer")',
            "$notes-core", text("Missing"), "LNil")),
        ("$empty-known", form("module:attach-layer", "$base", '(SyntaxOriginV1 "empty" "layer")',
            "$notes-core", text("Note"), "LNil")),
        ("$execution-language", form("module:language", "$base-presentation", text("Base"),
            text("Nat"), "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$execution-syntax", form("module:language-syntax", "$execution-language")),
        ("$execution-parser", form("langdef:compile-parser", "$execution-syntax", "auto")),
        ("$execution-parse", form("langdef:parser-parse-text", "$execution-parser", text("add(z,s(z))"))),
        ("$execution-authority", form("langdef:load-language-value", "$core")),
        ("$execution-projection", form("langdef:project-parser-result", "$execution-authority",
            text("Nat"), "$execution-parse")),
        ("$execution-term", form("langdef:projected-term", "$execution-projection", "0")),
        ("$execution-admission", form("langdef:admit-term-data", "$execution-authority", text("Nat"), "$execution-term")),
        ("$execution-rewrite", form("langdef:rewrite-term-at-data", "$execution-authority", text("Nat"), text("Nat"),
            "8", "100000", "(LangDef:RelationRows)", "$execution-term")),
        ("$execution-parser-close", form("langdef:close-parser", "$execution-parser")),
        ("$execution-authority-close", form("langdef:close-language", "$execution-authority")),
        ("$reader-close", form("langdef:close-parser", "$reader")),
    ]
    observed = ("base", "both", "both-again", "core", "note-fields", "hint-fields", "wrong-field",
                "conflict", "mismatch", "missing", "duplicate-fields", "law-layer", "empty-unknown", "empty-known",
                "execution-parse", "execution-term", "execution-admission", "execution-rewrite",
                "over-same", "over-different", "foreign-notes", "notes-core", "over-malformed",
                "over-malformed-wire", "over-conflict")
    record = "(ModuleLayersControlV1 " + " ".join("$" + key for key in observed) + ")"
    prefix = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n!'
    program = prefix + lets(bindings, "(function (return " + record + "))") + "\n"
    values = [v for v in run(binary, language, output, "module-layers", program)
              if headed(v, "ModuleLayersControlV1", len(observed))]
    if len(values) != 1:
        raise AssertionError((language, "layer workflow", values))
    result = dict(zip(observed, values[0][1:]))
    assert result["core"] == result["base"], result
    assert result["both"] == result["both-again"], result
    assert result["both"] == result["over-same"], result
    assert result["both"] == result["over-different"], result
    assert result["foreign-notes"][1] == result["notes-core"], result
    assert rows(result["foreign-notes"][2]) == rows(result["both"][2])[:1], result
    for key in ("over-malformed", "over-malformed-wire"):
        assert headed(result[key], "ModuleRefusedV1", 3), (key, result[key])
        assert result[key][1] == Symbol("malformed-layer-base"), (key, result[key])
    assert headed(result["over-conflict"], "ModuleRefusedV1", 3), result
    assert result["over-conflict"][1] == Symbol("layer-origin-conflict"), result
    assert headed(result["both"], "ModuleExtendedLanguageV1", 2), result
    assert len(rows(result["both"][2])) == 2, result
    assert rows(result["note-fields"]) == [(Symbol("SourceNote"),)], result
    assert rows(result["hint-fields"]) == [(Symbol("SearchDepth"),)], result
    for key in ("wrong-field", "conflict", "mismatch", "missing", "law-layer"):
        assert headed(result[key], "ModuleRefusedV1", 3), (key, result[key])
    assert result["conflict"][1] == Symbol("layer-origin-conflict"), result
    assert result["mismatch"][1] == Symbol("layer-base-mismatch"), result
    assert result["law-layer"][1] == Symbol("layer-elaboration-required"), result
    assert headed(result["empty-unknown"], "ModuleRefusedV1", 3), result
    assert headed(result["empty-known"], "ModuleExtendedLanguageV1", 2), result
    assert headed(result["execution-parse"], "ParserAcceptedV1", 2), result
    assert result["execution-term"] == (Symbol("NatAdd"), (Symbol("NatZ"),),
        (Symbol("NatS"), (Symbol("NatZ"),))), result
    assert result["execution-admission"] == (Symbol("LangDef:TermAdmitted"),), result
    rewritten = result["execution-rewrite"]
    assert headed(rewritten, "LangDef:ContextualTermResults", 2), rewritten
    assert rewritten[1] == Symbol("LangDef:ContextComplete"), rewritten
    assert headed(rewritten[2], "LangDef:ContextualTermResult", 2) and \
        rewritten[2][1] == (Symbol("NatS"), (Symbol("NatZ"),)), rewritten
    fields = rows(result["duplicate-fields"][2])[0][4]
    assert rows(fields) == [(Symbol("SourceNote"),)] * 2, result
    hostile = ('(= (SourceNote) host-note)\n(= (SearchDepth) host-hint)\n'
               '(= (ModuleLayerV1 $k $s $c $d) host-layer)\n'
               '(= (ModuleExtendedLanguageV1 $c $l) host-language)\n')
    hostile_values = [v for v in run(binary, language, output, "module-layers-hostile", hostile + program)
                      if headed(v, "ModuleLayersControlV1", len(observed))]
    assert hostile_values == values, (language, "host equations interpreted layer data")
    print(f"ModuleLayersSummary {language} 33 0")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--reader-artifact", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--language", action="append", choices=("he", "petta", "prime"))
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for language in args.language or ("he", "petta", "prime"):
        controls(args.cetta.resolve(), args.reader_artifact.resolve() if args.reader_artifact else None,
                 output, language)
        handoff_example(args.cetta.resolve(), output, language)


if __name__ == "__main__":
    main()
