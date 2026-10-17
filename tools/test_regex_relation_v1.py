#!/usr/bin/env python3
"""Qualify regex occurrences, persistent pull tokens and capture observations."""
import argparse
import json
from pathlib import Path

from gslt2parse_schema_v1 import StringLiteral, Symbol
from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed, output_values
from test_parser_artifacts_v1 import run, run_file
from test_regex_workflow_v1 import match


ROOT = Path(__file__).resolve().parents[1]


def record(binary, host, output, name, bindings, body):
    program = "!(import! &self re)\n!" + lets(bindings, form("function", form("return", body))) + "\n"
    values = run(binary, host, output, name, program)
    found = [value for value in values if headed(value, name)]
    if len(found) != 1:
        raise AssertionError((host, name, values))
    return found[0]


def check_host(binary, host, output):
    checks = 0
    p = form("re:compile", text("a|aa"))
    row = record(binary, host, output, "RelationPullV1", [
        ("$p", p), ("$token", form("re:relation-open", "$p", text("aa"))),
        ("$first", form("re:relation-next", "$token")),
        ("$again", form("re:relation-next", "$token")),
        ("$state", form("re:relation-progress", "$token")),
    ], form("RelationPullV1", "$first", "$again", "$state"))
    if row[1] != row[2] or not headed(row[1], "re:relation-yield", 2) or row[1][1] != match(0, 1) or \
            row[3] != (Symbol("re:relation-state"), 1, Symbol("Pending")):
        raise AssertionError((host, "persistent lazy pull", row))
    checks += 4

    row = record(binary, host, output, "RelationReplayV1", [
        ("$p", p), ("$token", form("re:relation-open", "$p", text("aa"))),
        ("$closed-pattern", form("re:close", "$p")),
        ("$all", form("collapse", form("re-internal:relation-answers", "$token"))),
        ("$again", form("collapse", form("re-internal:relation-answers", "$token"))),
        ("$first", form("re:relation-next", "$token")),
        ("$state", form("re:relation-progress", "$token")),
    ], form("RelationReplayV1", "$all", "$again", "$first", "$state"))
    expected = (match(0, 1), match(0, 2), match(1, 2))
    if row[1] != expected or row[2] != expected or not headed(row[3], "re:relation-yield", 2) or \
            row[3][1] != expected[0] or row[4] != (Symbol("re:relation-state"), 3, Symbol("Complete")):
        raise AssertionError((host, "multi-shot replay and independent program ownership", row))
    checks += 4

    cases = [
        ("a|aa", "aa", (match(0, 1), match(0, 2), match(1, 2))),
        ("(a|a)", "a", (match(0, 1, (0, 1)), match(0, 1, (0, 1)))),
        ("(a)|a", "a", (match(0, 1, (0, 1)), match(0, 1, None))),
        ("z", "aa", ()),
        ("", "aλ", (match(0, 0), match(1, 1), match(3, 3))),
        ("(?=(a|aa))\\1", "aa", (match(0, 1, (0, 1)), match(1, 2, (1, 2)))),
    ]
    for index, (pattern, sample, expected) in enumerate(cases):
        row = record(binary, host, output, "RelationOccurrencesV1", [
            ("$p", form("re:compile", text(pattern))),
            ("$all", form("collapse", form("re:match-relation", "$p", text(sample)))),
            ("$closed", form("re:close", "$p")),
        ], form("RelationOccurrencesV1", str(index), "$all"))
        if row[2] != expected:
            raise AssertionError((host, pattern, sample, expected, row))
        checks += 1

    row = record(binary, host, output, "RelationFullV1", [
        ("$p", p), ("$token", form("re:relation-open-at", "$p", text("aa"), "Full", "0")),
        ("$first", form("re:relation-next", "$token")),
    ], form("RelationFullV1", "$first"))
    if not headed(row[1], "re:relation-yield", 2) or row[1][1] != match(0, 2):
        raise AssertionError((host, "full eligibility", row))
    checks += 1

    row = record(binary, host, output, "RelationGroupV1", [
        ("$p", form("re:compile", text("(?<greek>λ)(a*)"))),
        ("$m", form("re:find", "$p", text("xλ"))),
        ("$span", form("re:group-span", "$m", text("greek"))),
        ("$text", form("re:group-text", "$m", text("xλ"), text("greek"))),
        ("$empty-span", form("re:group-span", "$m", "2")),
        ("$empty-text", form("re:group-text", "$m", text("xλ"), "2")),
        ("$missing", form("re:group-span", "$m", text("absent"))),
    ], form("RelationGroupV1", "$span", "$text", "$empty-span", "$empty-text", "$missing"))
    if row[1:5] != ((Symbol("re:span"), 1, 3), StringLiteral("λ"),
                   (Symbol("re:span"), 3, 3), StringLiteral("")) or \
            not headed(row[5], "re:failed", 2) or row[5][1] != Symbol("re:group-reference"):
        raise AssertionError((host, "named/empty/invalid groups", row))
    checks += 5
    row = record(binary, host, output, "RelationUnmatchedV1", [
        ("$p", form("re:compile", text("(a)?b"))),
        ("$m", form("re:find", "$p", text("b"))),
        ("$span", form("re:group-span", "$m", "1")),
        ("$text", form("re:group-text", "$m", text("b"), "1")),
    ], form("RelationUnmatchedV1", "$span", "$text"))
    if row[1:] != ((Symbol("re:unmatched"), 1),) * 2:
        raise AssertionError((host, "unmatched is not empty", row))
    checks += 2

    row = record(binary, host, output, "RegexBooleanBranchV1", [
        ("$p", form("re:compile", text("a"))),
        ("$yes", form("re:is-match", "$p", text("a"))),
        ("$no", form("re:is-match", "$p", text("b"))),
        ("$left", form("if", "$yes", "accepted-positive", "rejected-positive")),
        ("$right", form("if", "$no", "accepted-negative", "rejected-negative")),
        ("$or", form("if", form("or", "$no", "$yes"), "accepted-or", "rejected-or")),
        ("$and", form("if", form("and", "$yes", "$no"), "accepted-and", "rejected-and")),
    ], form("RegexBooleanBranchV1", "$left", "$right", "$or", "$and"))
    if row[1:] != tuple(map(Symbol, ("accepted-positive", "rejected-negative", "accepted-or", "rejected-and"))):
        raise AssertionError((host, "predicate booleans must drive actual conditionals", row))
    checks += 4

    bindings = [("$p", form("re:compile", text("a{64}"))),
                ("$token", form("re:relation-open", "$p", text("a" * 64)))]
    for i in range(10):
        bindings.append((f"$slice{i}", form("re:relation-next-bounded", "$token", "1")))
    bindings += [("$before", form("re:relation-progress", "$token")),
                 ("$first", form("re:relation-next", "$token")),
                 ("$again", form("re:relation-next", "$token")),
                 ("$after", form("re:relation-progress", "$token"))]
    row = record(binary, host, output, "RelationBudgetV1", bindings,
                 form("RelationBudgetV1", "$slice0", "$slice9", "$before", "$first", "$again", "$after"))
    if any(not headed(row[i], "re:relation-pending", 2) for i in (1, 2)) or \
            row[3] != (Symbol("re:relation-state"), 0, Symbol("Pending")) or row[4] != row[5] or \
            not headed(row[4], "re:relation-yield", 2) or row[4][1] != match(0, 64) or \
            row[6] != (Symbol("re:relation-state"), 1, Symbol("Pending")):
        raise AssertionError((host, "retained budget residual", row))
    checks += 6

    row = record(binary, host, output, "RelationCloseV1", [
        ("$p", p), ("$token", form("re:relation-open", "$p", text("aa"))),
        ("$first", form("re:relation-next", "$token")),
        ("$closed", form("re:relation-close", "$token")),
        ("$after", form("re:relation-next", "$token")),
    ], form("RelationCloseV1", "$closed", "$after"))
    if row[1] != Symbol("re:closed") or not headed(row[2], "re:failed", 2) or \
            row[2][1] != Symbol("re:invalid-handle"):
        raise AssertionError((host, "explicit close is not completed absence", row))
    checks += 2

    row = record(binary, host, output, "RelationDemandV1", [
        ("$p", p), ("$token", form("re:relation-open", "$p", text("aa"))),
        ("$first", form("once", form("re-internal:relation-answers", "$token"))),
        ("$state", form("re:relation-progress", "$token")),
    ], form("RelationDemandV1", "$first", "$state"))
    if row[1] != match(0, 1) or not headed(row[2], "re:relation-state", 2):
        raise AssertionError((host, "first-answer readout", row))
    lazy = row[2] == (Symbol("re:relation-state"), 1, Symbol("Pending"))
    if host.startswith("petta") and not lazy:
        raise AssertionError((host, "lost incremental nondeterministic demand", row))
    checks += 1
    for example in ("regex_relation_v1", "regex_capture_access_v1"):
        source = ROOT / "examples" / (example + ".metta")
        expected = output_values(source.with_suffix(".expected").read_text())
        actual = run_file(binary, host, output, example, source)
        if actual != expected:
            raise AssertionError((host, example, expected, actual))
        checks += len(expected)
    return checks, {"persistentPull": True, "incrementalNondeterministicOnce": lazy,
                    "demandedOccurrencesBeforeFirstReadout": row[2][1]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/regex-relation-public")
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    count = 0; capabilities = {}
    for host in ("he", "petta", "petta-1.1", "prime"):
        passed, capabilities[host] = check_host(args.cetta.resolve(), host, output)
        count += passed
    (output / "capabilities.json").write_text(json.dumps(capabilities, indent=2) + "\n")
    print(f"(RegexRelationPublicV1Summary {count} 0)")
    print(json.dumps(capabilities, sort_keys=True))


if __name__ == "__main__":
    main()
