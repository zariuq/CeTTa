#!/usr/bin/env python3
"""Check public definition preparation, weak sharing and spelling queries."""
import argparse
from pathlib import Path

from gslt2parse_schema_v1 import StringLiteral, Symbol
from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed
from test_parser_artifacts_v1 import run, run_file
from test_regex_workflow_v1 import match, node, list_value

ROOT = Path(__file__).resolve().parents[1]


def check_host(binary, host, output):
    checks = 0

    def record(name, bindings, body, prefix=""):
        source = "!(import! &self re)\n" + prefix + "!" + lets(
            bindings, form("function", form("return", form(name, *body)))) + "\n"
        values = run(binary, host, output, name, source)
        rows = [row for row in values if headed(row, name)]
        if len(rows) != 1:
            raise AssertionError((host, name, values))
        return rows[0]

    definition = form("re:pattern", text("a|ab"))
    row = record("RegexImplicitDefinitionV1", [
        ("$d", definition), ("$found", form("re:find", "$d", text("ab"))),
        ("$full", form("re:full-match", "$d", text("ab"))),
        ("$p", form("re:prepare", "$d")), ("$q", form("re:prepare", "$d")),
    ], ["$d", "$found", "$full", "$p", "$q"])
    assert row[1] == node("re:pattern", StringLiteral("a|ab")) and row[2:4] == (match(0, 1), match(0, 2)), (host, row)
    assert row[4] != row[5] and headed(row[4], "NativeHandle", 2), (host, row)
    checks += 4

    row = record("RegexDefinitionCloseV1", [
        ("$d", definition), ("$p", form("re:prepare", "$d")),
        ("$q", form("re:prepare", "$d")), ("$closed", form("re:close", "$p")),
        ("$r", form("re:prepare", "$d")), ("$stale", form("re:find", "$q", text("ab"))),
        ("$fresh", form("re:find", "$r", text("ab"))),
    ], ["$p", "$q", "$r", "$stale", "$fresh"])
    assert row[1] != row[2] and row[1] != row[3], (host, row)
    assert row[4] == match(0, 1), (host, row)
    assert row[5] == match(0, 1), (host, row)
    checks += 4

    row = record("RegexDefinitionAfterMutationV1", [
        ("$p", form("re:prepare", definition)),
        ("$changed", "(add-atom &self (RegexMemoDependencyV1 marker))"),
        ("$q", form("re:prepare", definition)),
        ("$answer", form("re:find", "$q", text("ab"))),
    ], ["$p", "$q", "$answer"])
    assert row[1] != row[2] and row[3] == match(0, 1), (host, row)
    checks += 2

    names = "".join(f"(RegexNamedSymbolV1 {name})\n" for name in
                    ("audit-name:alpha", "audit-name:λ", "audit-name:alpine", "audit-name:other"))
    row = record("RegexSymbolQueryV1", [
        ("$p", form("re:prepare", form("re:pattern", text("audit-name:(?:alpha|λ)")))),
        ("$first", form("re:symbol-query-with", "$p", "Full")),
        ("$changed", "(add-atom &self audit-name:new)"),
        ("$second", form("re:symbol-query-with", form("re:pattern", text("audit-name:new")), "Full")),
        ("$look", form("re:symbol-query-with", form("re:pattern", text("audit-name:alpha(?=$)")), "Full")),
        ("$absent", form("re:symbol-query-with", form("re:pattern", text("audit-name:missing")), "Full")),
    ], ["$first", "$second", "$look", "$absent"], names)
    expected = [list_value([Symbol("audit-name:alpha"), Symbol("audit-name:λ")]),
                list_value([Symbol("audit-name:new")]), list_value([Symbol("audit-name:alpha")]), Symbol("LNil")]
    for result, wanted in zip(row[1:], expected):
        assert headed(result, "re:symbols", 2) and result[1] == wanted, (host, result, wanted)
        assert headed(result[2], "re:name-snapshot", 2) and result[2][2] == 0, (host, result)
        checks += 2

    expected_examples = {
        "definitions": [match(0, 1), node("RegexDefinitionCaptureV1", node("re:pattern", StringLiteral("(?<letter>λ)")), StringLiteral("λ")),
                        Symbol("re:no-match"), Symbol("RegexDefinitionRejectedV1")],
        "literal_trie": [node("RegexLiteralPriorityV1", match(0, 1)), node("RegexLiteralStartV1", match(0, 3)),
                         node("RegexLiteralFullV1", match(0, 2)), node("RegexLiteralAbsentV1", Symbol("re:no-match"))],
        "symbol_query": [node("RegexSymbolNamesV1", list_value([Symbol("trie-name:alpha"), Symbol("trie-name:λ")])),
                         node("RegexSymbolPrefixV1", list_value([Symbol("trie-name:alpha")])),
                         node("RegexSymbolAbsentV1", Symbol("LNil")),
                         node("RegexSymbolLookV1", list_value([Symbol("trie-name:alpha")]))],
    }
    for example, expected in expected_examples.items():
        actual = run_file(binary, host, output, "example-" + example, ROOT / f"examples/regex_{example}_v1.metta")
        assert actual == expected, (host, example, actual, expected)
        checks += len(expected)
    return checks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/regex-fastpaths-public-v1")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    checks = sum(check_host(args.cetta.resolve(), host, args.output.resolve())
                 for host in ("he", "petta", "petta-1.1", "prime"))
    print(f"(RegexFastpathsPublicV1Summary {checks} 0)")


if __name__ == "__main__":
    main()
