#!/usr/bin/env python3
"""Check declared family selection against independent late CST observation."""

import argparse
from pathlib import Path
import unicodedata

from gslt2parse_schema_v1 import StringLiteral, Symbol, parse_sexprs
from test_composable_syntax_v1 import form, lets, parts, rule, text
from test_lbnf_values_v1 import headed
from test_parser_artifacts_v1 import run, run_file

ROOT = Path(__file__).resolve().parents[1]


def declaration(label="re-src:backref", class_name="RxDigitClass"):
    return form("ParserSelectionV1", form("LCons",
        form("ParserRejectFollowingClassV1", text(label), text(class_name)), "LNil"))


def maximal_numeric_tree(tree, source):
    pending = [tree]
    while pending:
        value = pending.pop()
        if not isinstance(value, tuple):
            continue
        if (len(value) >= 4 and value[0] == Symbol("CstRuleV1") and
                value[1] == StringLiteral("re-src:backref")):
            stop = value[3]
            if stop < len(source) and unicodedata.category(source[stop]) == "Nd":
                return False
        pending.extend(value)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/parser-family-selection-v1")
    arguments = parser.parse_args()
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    checks = 0
    for host in ("he", "petta", "petta-1.1", "prime"):
        queries = []
        cases = [("single", r"\12", declaration(), "numeric"),
                 ("product", r"\12\34\56\78", declaration(), "numeric"),
                 ("mixed-decimal", "\\١2\\３4", declaration(), "numeric"),
                 ("nested-reference", r"(a|\12)+\34", declaration(), "numeric"),
                 ("neutral", r"\12\34", "(ParserSelectionV1 LNil)", "neutral"),
                 ("none-selected", r"\1a", declaration(class_name="RxLiteralClass"), "none")]
        for name, source, selection, _ in cases:
            observation = lets([
                ("$reader", form("langdef:load-baked-parser", text("regex-pattern-reader"))),
                ("$ordinary", form("langdef:parser-parse-text-forest", "$reader", text(source), "0")),
                ("$selected", form("langdef:parser-parse-text-forest-selected",
                                   "$reader", text(source), selection, "0")),
                ("$view", form("langdef:parser-parse-text-selected", "$reader", text(source), selection)),
                ("$closed", form("langdef:close-parser", "$reader")),
            ], "(case ($ordinary $selected) "
               "((((ParserWithForestV1 (ParserAcceptedV1 $all $span) $forest) "
               "(ParserWithForestV1 (ParserAcceptedV1 $kept $selected-span) $selected-forest)) "
               "(let $preserved (== $forest $selected-forest) "
               f"(function (return (FamilySelectionControlV1 {text(name)} $all $kept $preserved "
               "$view (ParserAcceptedV1 $kept $selected-span)))))) "
               "($other (function (return (UnexpectedFamilySelectionV1 $other))))))")
            queries.append("!" + observation)
        for name, selection in (
            ("unknown-class", declaration(class_name="MissingClass")),
            ("unknown-label", declaration(label="MissingLabel")),
            ("class-nul-is-not-prefix", declaration(class_name="RxDigitClass\0suffix")),
            ("label-nul-is-not-prefix", declaration(label="re-src:backref\0suffix")),
            ("unknown-guard", "(ParserSelectionV1 (LCons (UnknownFamilyObservationV1) LNil))"),
            ("malformed-descriptor", "(ParserSelectionV1 NotAList)"),
        ):
            bindings = [("$reader", form("langdef:load-baked-parser", text("regex-pattern-reader")))]
            if name in ("class-nul-is-not-prefix", "label-nul-is-not-prefix"):
                identity = ("RxDigitClass" if name.startswith("class-") else "re-src:backref") + "\0suffix"
                bindings.append(("$nul-identity", form("str:from-codepoints",
                    "(" + " ".join(str(ord(character)) for character in identity) + ")")))
                selection = selection.replace(text(identity), "$nul-identity")
            obligation = form("ParserWithForestV1",
                form("ParserSelectionObligationV1", "$request", "$reason"), "$forest")
            summary = lets([("$same-request", form("==", "$request", selection))],
                form("function", form("return", form("ParserWithForestV1",
                    form("ParserSelectionObligationV1", "$same-request", "$reason"), "$forest"))))
            summarized = form("case", "$observed", form(
                form(obligation, summary),
                form("$other", form("function", form("return", "$other")))))
            queries.append("!" + lets(bindings + [
                ("$observed", form("langdef:parser-parse-text-forest-selected",
                                  "$reader", text(r"\12"), selection, "0")),
                ("$view", form("langdef:parser-parse-text-selected", "$reader", text(r"\12"), selection)),
                ("$same-view", form("==", "$observed", "$view")),
                ("$closed", form("langdef:close-parser", "$reader")),
                ("$summarized", summarized),
            ], form("function", form("return", form("FamilySelectionOutcomeV1", text(name), "$summarized", "$same-view")))))
        cyclic_language, cyclic_profile, _, _ = parts("CyclicSelectedView", [
            rule("Loop", "", [("x", "E")], [form("SyntaxNonTerminal", text("x"))]),
            rule("Empty", "", syntax=[])])
        for name, loader, source, expected in (
            ("rejected", form("langdef:load-baked-parser", text("regex-pattern-reader")), "(", "ParserRejectedV1"),
            ("cyclic", form("langdef:load-authored-parser-values", cyclic_language, cyclic_profile), "", "ParserCyclicV1"),
        ):
            queries.append("!" + lets([
                ("$reader", loader),
                ("$forest-result", form("langdef:parser-parse-text-forest-selected", "$reader", text(source), "(ParserSelectionV1 LNil)", "0")),
                ("$view", form("langdef:parser-parse-text-selected", "$reader", text(source), "(ParserSelectionV1 LNil)")),
                ("$same", form("==", "$forest-result", "$view")),
                ("$closed", form("langdef:close-parser", "$reader")),
            ], form("function", form("return", form("FamilySelectionDiagnosticV1", text(name), "$same", "$view")))))
        program = "!(import! &self langdef)\n!(import! &self str)\n" + "\n".join(queries) + "\n"
        observed = run(arguments.cetta.resolve(), host, output, "families", program)
        controls = {value[1].text: value for value in observed
                    if headed(value, "FamilySelectionControlV1", 6)}
        assert len(controls) == len(cases), (host, "missing selection observations", observed)
        for name, source, _, mode in cases:
            row = controls[name]
            all_trees, kept, preserved, view, reference = row[2:]
            expected = (tuple(tree for tree in all_trees if maximal_numeric_tree(tree, source))
                        if mode == "numeric" else all_trees if mode == "neutral" else ())
            assert kept == expected, (host, name, "ordered early/late observation mismatch")
            assert preserved in (Symbol("True"), Symbol("true")), (host, name, "raw forest changed")
            assert headed(view, "ParserAcceptedV1", 2) and view == reference, (host, name, "selected view changed values or span")
            checks += 3
        outcomes = {value[1].text: value[2] for value in observed
                    if headed(value, "FamilySelectionOutcomeV1", 3)}
        comparisons = [value[3] for value in observed if headed(value, "FamilySelectionOutcomeV1", 3)]
        assert len(comparisons) == 6 and all(value in (Symbol("True"), Symbol("true")) for value in comparisons), (host, "selected view discarded obligation evidence")
        checks += len(comparisons)
        for name in ("unknown-class", "unknown-label", "unknown-guard",
                     "class-nul-is-not-prefix", "label-nul-is-not-prefix"):
            value = outcomes[name]
            assert headed(value, "ParserWithForestV1", 2), (host, name, "forest was discarded")
            assert headed(value[1], "ParserSelectionObligationV1", 2), (host, name, value[1])
            assert value[1][1] in (Symbol("True"), Symbol("true")), (host, name, "request changed")
            assert headed(value[2], "pf-v1", 5), (host, name, "missing raw evidence")
            checks += 1
        assert headed(outcomes["malformed-descriptor"], "ParserFailureV1", 1)
        checks += 1
        diagnostics = {value[1].text: value for value in observed if headed(value, "FamilySelectionDiagnosticV1", 3)}
        for name, kind, arity in (("rejected", "ParserRejectedV1", 3), ("cyclic", "ParserCyclicV1", 1)):
            value = diagnostics[name]
            assert value[2] in (Symbol("True"), Symbol("true")), (host, name, "diagnostic evidence changed")
            assert headed(value[3], "ParserWithForestV1", 2) and headed(value[3][1], kind, arity), (host, name, "diagnostic forest missing", value[3])
            checks += 1
        expected_examples = parse_sexprs((ROOT / "examples/regex_packed_selection_v1.expected").read_text())
        actual_examples = run_file(arguments.cetta.resolve(), host, output, "examples",
                                   ROOT / "examples/regex_packed_selection_v1.metta")
        assert actual_examples == expected_examples, (host, "example mismatch", actual_examples)
        checks += len(expected_examples)
    print(f"(ParserFamilySelectionPublicV1Summary {checks} 0)")


if __name__ == "__main__":
    main()
