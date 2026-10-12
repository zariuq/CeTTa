#!/usr/bin/env python3
"""Qualify baked general parsers and atomic compiler publication."""

import argparse
import json
from pathlib import Path
import subprocess

from gslt2parse_schema_v1 import Symbol

from parser_artifact_build_v1 import Inputs, compile_catalog, load_manifest
from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed
from test_parser_artifacts_v1 import run

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/parser-baking-public")
    args = parser.parse_args()
    binary, output = args.cetta.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    samples = [("bnf-reader", '<S> ::= "a"\n'), ("ebnf-reader", '<S> ::= "a"*\n'),
               ("lbnf-reader", 'One. S ::= "a";'), ("bnf-program", "aaa"),
               ("ebnf-program", "aaa"), ("joined-left", "a"),
               ("joined-right", "a"), ("joined-parser", "a"), ("token-word", " /* note */ ÿz "),
               ("token-joined", "a"), ("predictive-left", "a"),
               ("predictive-right", "b"), ("predictive-joined", "b")]
    count = 0
    for language in ("prime", "he", "petta"):
        for name, sample in samples:
            path = output / f"{language}-{name}.tpp"
            path_again = output / f"{language}-{name}-again.tpp"
            program = "!(import! &self langdef)\n!" + lets([
                ("$baked", form("langdef:load-baked-parser", text(name))),
                ("$baked-value", form("langdef:parser-parse-text", "$baked", text(sample))),
                ("$baked-evidence", form("langdef:parse-authored-text-evidence", "$baked", text(sample), "50000")),
                ("$frozen", form("langdef:freeze-parser", "$baked", text(str(path)))),
                ("$restored", form("langdef:load-parser-artifact-checked", text(str(path)))),
                ("$info", form("langdef:parser-info", "$restored")),
                ("$closed-baked", form("langdef:close-parser", "$baked")),
                ("$restored-value", form("langdef:parser-parse-text", "$restored", text(sample))),
                ("$restored-evidence", form("langdef:parse-authored-text-evidence", "$restored", text(sample), "50000")),
                ("$refrozen", form("langdef:freeze-parser", "$restored", text(str(path_again)))),
                ("$closed-restored", form("langdef:close-parser", "$restored")),
                ("$unknown", form("langdef:load-baked-parser", text("not-in-the-catalog"))),
            ], "(function (return (BakedParserControlV1 $baked-value $restored-value "
               "$baked-evidence $restored-evidence $frozen $refrozen $info $unknown)))") + "\n"
            values = [value for value in run(binary, language, output, name, program)
                      if headed(value, "BakedParserControlV1", 8)]
            if len(values) != 1:
                raise AssertionError((language, name, values))
            record = values[0]
            if not headed(record[1], "ParserAcceptedV1", 2) or record[1] != record[2]:
                raise AssertionError((language, name, "values/spans/multiplicity", record[:3]))
            if name == "joined-parser" and len(record[1][1]) != 2:
                raise AssertionError((language, name, "baked ambiguity was collapsed", record[1]))
            if record[3] != record[4] or not headed(record[3], "LangDef:AuthoredParseEvidenceAccepted", 6):
                raise AssertionError((language, name, "canonical evidence", record[3:5]))
            if not headed(record[5], "ParserArtifactV1", 2) or record[5] != record[6] or path.read_bytes() != path_again.read_bytes():
                raise AssertionError((language, name, "artifact identity changed on refreeze"))
            if not headed(record[7], "ParserInfoV1", 6) or record[7][4] != 2:
                raise AssertionError((language, name, "preparation sharing", record[7]))
            if name in ("token-word", "predictive-left", "predictive-right", "predictive-joined") and record[7][2:4] != (Symbol("auto"), Symbol("predictive")):
                raise AssertionError((language, name, "baked predictive program was not restored", record[7]))
            if name == "token-joined" and (record[7][3] != Symbol("gll") or
                                           not headed(record[7][6], "ParserSelectConflictV1", 6)):
                raise AssertionError((language, name, "layout SELECT conflict must retain the general engine", record[7]))
            if not headed(record[8], "ParserFailureV1", 1):
                raise AssertionError((language, name, "unknown catalog name", record[8]))
            count += 7
        print(f"PASS: {language} baked general readers and ambiguous composition")

    # A completed first parser cannot be published when a subsequent parser
    # fails. The previously installed header and generation remain intact.
    bad_source = output / "unbound.cf"
    bad_source.write_text('Bad. S ::= Undeclared;\n', encoding="utf-8")
    manifest = output / "negative-build.json"
    manifest.write_text(json.dumps({"format": "ParserBuildManifestV1", "parsers": [
        {"name": "first", "format": "lbnf", "source": str(ROOT / "langdef/parsers/joined_left_v1.cf"), "start": "S"},
        {"name": "bad", "format": "lbnf", "source": "unbound.cf", "start": "S"}]}))
    header, generations = output / "published.h", output / "generations"
    header.write_text("previous catalog\n")
    old_header = header.read_bytes()
    generations.mkdir(exist_ok=True)
    before = set(generations.iterdir())
    for timeout in (60, 0.000001):
        try:
            compile_catalog(binary, manifest, load_manifest(manifest), generations, header, timeout)
        except (ValueError, subprocess.TimeoutExpired):
            pass
        else:
            raise AssertionError("failed/interrupted generation was published")
        if header.read_bytes() != old_header or set(generations.iterdir()) != before:
            raise AssertionError("failed/interrupted generation changed installed state")
        count += 2

    for rows in ([{"name": "duplicate", "format": "reader", "reader": "lbnf"}] * 2,
                 [{"name": "cycle", "format": "join", "operands": ["cycle", "cycle"], "start": "S"}]):
        manifest.write_text(json.dumps({"format": "ParserBuildManifestV1", "parsers": rows}))
        try:
            entries = load_manifest(manifest)
            Inputs(manifest, entries).syntax("cycle")
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate/cyclic catalog accepted")
        count += 1
    print(f"(ParserBakingPublicV1Summary {count} 0)")


if __name__ == "__main__":
    main()
