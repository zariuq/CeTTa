#!/usr/bin/env python3
"""Qualify module-authored syntax, selected captures and owner lifetimes."""
import argparse
import json
from pathlib import Path
import time

from gslt2parse_schema_v1 import StringLiteral, Symbol
from test_composable_syntax_v1 import form, lets, parts, rule, text
from test_lbnf_values_v1 import headed
from test_parser_artifacts_v1 import run, run_file
from test_regex_selection_v1 import observe, portable_patterns, python_observation, source

ROOT = Path(__file__).resolve().parents[1]


def node(head, *args):
    return (Symbol(head), *args)


def list_value(items):
    result = Symbol("LNil")
    for item in reversed(items):
        result = node("LCons", item, result)
    return result


def match(start, end, *groups):
    return node("re:match", start, end, list_value([
        node("re:unmatched", i) if span is None else node("re:capture-span", i, *span)
        for i, span in enumerate(((start, end), *groups))]))


def packed(value, observation, count=None):
    return (headed(value, "ParserWithForestV1", 2) and
            headed(value[1], observation, 2 if observation == "ParserAcceptedV1" else 3) and
            headed(value[2], "pf-v1", 5) and
            (count is None or len(value[1][1]) == count))


def check_examples(binary, host, output):
    for name in ("syntax", "selected", "constructed", "artifacts", "rendering", "correspondence", "unicode", "flags", "templates",
                 "class_operations", "lookaround", "backreferences", "lookbehind_policy", "resources"):
        values = run_file(binary, host, output, "example-" + name,
                          ROOT / f"examples/regex_{name}_v1.metta")
        if name == "syntax":
            assert len(values) == 1 and headed(values[0], "RegexSyntaxV1", 3)
            assert packed(values[0][1], "ParserAcceptedV1", 1)
            assert packed(values[0][2], "ParserAcceptedV1", 1)
            assert packed(values[0][3], "ParserRejectedV1")
        elif name == "selected":
            assert len(values) == 3 and values[0] == node("RegexSelectionV1", match(0, 1), match(0, 2))
            assert values[1] == match(0, 2, (1, 2))
            assert headed(values[2], "re:source-observation", 1) and packed(values[2][1], "ParserRejectedV1")
        elif name == "constructed":
            assert values == [match(1, 3), node("re:matches", list_value([
                match(0, 0), match(1, 1), match(3, 3)])), Symbol("re:no-match")]
        elif name == "artifacts":
            assert len(values) == 3 and headed(values[0], "re:match", 3) and values[0][1:3] == (1, 5)
            assert values[0][3][2][1] == node("re:named-group", StringLiteral("word"), node("re:capture-span", 1, 1, 5))
            assert values[1] == match(1, 2) and headed(values[2], "re:failed", 2)
        elif name == "rendering":
            assert len(values) == 4
            assert values[:3] == [node("re:pieces", list_value([
                StringLiteral(""), StringLiteral("a"), StringLiteral("λ"), StringLiteral("")])),
                StringLiteral("$1b$1"), StringLiteral("b<aa>")]
            assert headed(values[3], "re:failed", 2) and values[3][1] == Symbol("re:template")
        elif name == "unicode":
            assert values == [match(1, 7), match(1, 5), Symbol("re:no-match")]
        elif name == "flags":
            assert values == [match(0, 3, (0, 3)), match(0, 6), Symbol("re:no-match")]
        elif name == "templates":
            assert len(values) == 3
            assert values[:2] == [StringLiteral("b:a:$:ab"), StringLiteral("a<λ>b")]
            assert headed(values[2], "re:failed", 2) and values[2][1] == Symbol("re:template-reference")
        elif name == "class_operations":
            assert values == [match(2, 6), match(1, 5), Symbol("re:no-match")]
        elif name == "lookaround":
            assert values == [match(0, 4), match(3, 6), Symbol("re:no-match")]
        elif name == "backreferences":
            assert values == [match(0, 7, (0, 3)), match(0, 4, (0, 1)), Symbol("re:no-match")]
        elif name == "lookbehind_policy":
            assert values[:2] == [match(3, 3, (0, 2), (2, 3)), match(3, 3, (0, 1), (1, 3))]
            assert len(values) == 3 and headed(values[2], "re:incomplete", 1)
        elif name == "resources":
            assert values[:2] == [match(0, 4, (0, 4)), match(0, 4, (0, 4))]
            assert len(values) == 3 and headed(values[2], "re:incomplete", 1)
        else:
            assert len(values) == 5
            assert values[0] == node("RegexPriorityConnectionV1",
                match(0, 1), match(0, 2), Symbol("re:no-match"))
            assert values[1] == node("RegexCaptureConnectionV1",
                match(0, 1, None), match(0, 1, (0, 0)), match(0, 1, None))
            assert headed(values[2], "RegexRepetitionConnectionV1", 3)
            assert values[2][1] in (Symbol("True"), Symbol("true"))
            assert values[2][2:] == (match(0, 1, (0, 1)), Symbol("re:no-match"))
            assert headed(values[3], "RegexSourceConnectionV1", 3)
            assert values[3][1:3] == (match(1, 3, (1, 3)), match(1, 3, (1, 3)))
            assert headed(values[3][3], "re:source-observation", 1)
            assert packed(values[3][3][1], "ParserRejectedV1")
            assert headed(values[4], "RegexOutcomeConnectionV1", 3)
            assert values[4][1:3] == (match(0, 3), Symbol("re:no-match"))
            assert headed(values[4][3], "re:incomplete", 1)
    return 55


def check_generated(binary, output, count, seed):
    inputs = ("", "a", "b", "aa", "ab", "ba", "aab", "aba", "aaa", "aλb")
    queries, retained_queries, expected = [], [], []
    for pattern in portable_patterns(seed, count):
        regex, value, _ = source(pattern)
        bindings = [("$source", form("re:compile", text(regex))),
                    ("$value", form("re:compile-value", value))]
        variables = []
        for input_ in inputs:
            for operation, mode in (("find", "search"), ("full-match", "full")):
                observation = python_observation(regex, input_, mode)
                want = (Symbol("re:no-match") if observation["outcome"] == "no-match" else
                        match(*observation["groups"][0], *observation["groups"][1:]))
                for owner in ("$source", "$value"):
                    variable = "$r" + str(len(variables))
                    bindings.append((variable, form("re:" + operation, owner, text(input_))))
                    variables.append(variable)
                    expected.append(want)
        bindings += [("$closed-source", form("re:close", "$source")),
                     ("$closed-value", form("re:close", "$value"))]
        retained_queries.append("!" + lets(bindings, form("function", form("return",
            form("RegexGeneratedV1", "$source", "$value", *variables)))))
        queries.append("!" + lets(bindings, form("function", form("return",
            form("RegexGeneratedV1", *variables)))))
    checks = 0
    for host in ("he", "petta", "prime"):
        for retained, program_queries in ((False, queries), (True, retained_queries)):
            name = "generated-retained" if retained else "generated"
            values = run(binary, host, output, name, "!(import! &self re)\n" + "\n".join(program_queries) + "\n")
            fields = 2 if retained else 0
            rows = [value for value in values if headed(value, "RegexGeneratedV1", len(inputs) * 4 + fields)]
            if retained:
                for index, row in enumerate(rows):
                    assert headed(row[1], "NativeHandle", 2) and headed(row[2], "NativeHandle", 2), (host, index, "compile failed", row[1:3])
            actual = [entry for row in rows for entry in row[fields+1:]]
            assert len(actual) == len(expected), (host, name, "generated missing/extra observations", len(actual), len(expected))
            for index, (got, want) in enumerate(zip(actual, expected)):
                assert got == want, (host, name, index, "generated source/value/Python disagreement", got, want)
                checks += 1
    print(f"(RegexGeneratedWorkflowV1Summary {checks} 0)")


def check_count_identities(binary, output, count, seed):
    queries = []
    for pattern in portable_patterns(seed, count):
        regex, _, _ = source(pattern)
        # Wrapping finite-nullable and nested-star bodies deliberately tests
        # nullable unbounded loops without borrowing Python's loop policy.
        for body in (regex, f"(?:{regex})*"):
            for low, operator in ((0, "*"), (1, "+")):
                for lazy in ("", "?"):
                    queries.append("!" + lets([
                        ("$counted", form("re:compile", text(f"(?:{body}){{{low},}}{lazy}"))),
                        ("$direct", form("re:compile", text(f"(?:{body}){operator}{lazy}"))),
                        ("$one", form("re:store", "$counted")),
                        ("$two", form("re:store", "$direct")),
                        ("$same", form("==", "$one", "$two")),
                        ("$closed-one", form("re:close", "$counted")),
                        ("$closed-two", form("re:close", "$direct")),
                    ], '(function (return (RegexCountIdentityV1 $same)))'))
    checks = 0
    for host in ("he", "petta", "prime"):
        values = run(binary, host, output, "count-identities", "!(import! &self re)\n" + "\n".join(queries) + "\n")
        rows = [value for value in values if headed(value, "RegexCountIdentityV1", 1)]
        assert len(rows) == len(queries), (host, "missing identity observations")
        for index, row in enumerate(rows):
            assert row[1] in (Symbol("True"), Symbol("true")), (host, index, "count/direct artifact differs", row)
            checks += 1
    print(f"(RegexCountIdentityV1Summary {checks} 0)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/regex-workflow-v1")
    parser.add_argument("--oracle", type=Path, help="optional pinned Rust oracle; not a build dependency")
    parser.add_argument("--scale", action="store_true", help="exercise large textual compilation; report whole-process timings")
    parser.add_argument("--generated", type=int, default=0, metavar="COUNT", help="qualify generated text/value patterns against the portable Python profile")
    parser.add_argument("--count-identities", type=int, default=0, metavar="COUNT", help="qualify authored {0,}/star and {1,}/plus graph identities, including nullable nests")
    parser.add_argument("--seed", type=int, default=20261016)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.generated < 0 or args.count_identities < 0:
        parser.error("COUNT must be nonnegative")
    if args.count_identities:
        check_count_identities(args.cetta.resolve(), args.output.resolve(), args.count_identities, args.seed)
        return
    if args.generated:
        check_generated(args.cetta.resolve(), args.output.resolve(), args.generated, args.seed)
        return
    if args.scale:
        for name, pattern in (("literal-1000", "a" * 1000),
                              ("count-100000", "a{100000}"),
                              ("keywords-10000", "|".join(f"keyword{i:05d}" for i in range(10000))),
                              ("numeric-references-64", "(a)" * 12 + r"\12" * 64)):
            program = '!(import! &self re)\n!(import! &self system)\n!' + lets([
                ("$started", "(system:monotonic-ns)"),
                ("$pattern", form("re:compile", text(pattern))),
                ("$compiled", "(system:monotonic-ns)"),
                ("$result", form("re:full-match", "$pattern", text(""))),
                ("$matched", "(system:monotonic-ns)"),
                ("$compile-ns", "(- $compiled $started)"),
                ("$match-ns", "(- $matched $compiled)"),
                ("$closed", form("re:close", "$pattern")),
            ], '(function (return (RegexScaleV1 $result $compile-ns $match-ns)))') + '\n'
            started = time.perf_counter()
            values = run(args.cetta.resolve(), "he", args.output.resolve(), name, program)
            rows = [row for row in values if headed(row, "RegexScaleV1", 3)]
            assert len(rows) == 1 and rows[0][1] == Symbol("re:no-match"), (name, values)
            compile_ns, match_ns = rows[0][2:]
            assert isinstance(compile_ns, int) and compile_ns >= 0, (name, rows)
            assert isinstance(match_ns, int) and match_ns >= 0, (name, rows)
            print(json.dumps(dict(case=name, source_bytes=len(pattern.encode()),
                compile_ns=compile_ns, match_ns=match_ns,
                whole_process_seconds=round(time.perf_counter() - started, 6))), flush=True)
        return
    queries, predicates, oracle_cases = [], {}, {}

    def add(name, expression, expected=None, predicate=None):
        queries.append("!" + lets([("$answer", expression)],
            form("function", form("return", form("RegexWorkflowCaseV1", text(name), "$answer")))))
        predicates[name] = predicate or (lambda result, expected=expected: result == expected)

    def source_case(name, pattern, input_, expected, operation="find", oracle=True):
        if oracle:
            oracle_cases[name] = (pattern, input_, "full" if operation == "full-match" else
                                  "all" if operation == "find-all" else "search")
        add(name, lets([
            ("$pattern", form("re:compile", text(pattern))),
            ("$result", form("re:" + operation, "$pattern", text(input_))),
            ("$closed", form("re:close", "$pattern")),
        ], "$result"), expected)

    source_case("ordered", "a|ab", "ab", match(0, 1))
    source_case("full-constrain-first", "a|ab", "ab", match(0, 2), "full-match")
    source_case("greedy", "a+", "aaa", match(0, 3))
    source_case("lazy", "a+?", "aaa", match(0, 1))
    source_case("lazy-full", "a+?", "aaa", match(0, 3), "full-match")
    source_case("nullable-last", "(a|)*", "aa", match(0, 2, (1, 2)))
    source_case("nullable-empty", "(a?)*", "b", match(0, 0, (0, 0)))
    source_case("capture-persists", "(?:(a)|b)+", "ab", match(0, 2, (0, 1)))
    source_case("branch-rollback", "(a)c|ab", "ab", match(0, 2, None))
    source_case("unmatched", "(a)?b", "b", match(0, 1, None))
    source_case("empty-participating", "(a*)b", "b", match(0, 1, (0, 0)))
    source_case("elided-group", "(a){0}", "", match(0, 0, None))
    source_case("elided-middle", "(a){0}(b)", "b", match(0, 1, None, (0, 1)))
    source_case("elided-suffix", "(a)(b){0}", "a", match(0, 1, (0, 1), None))
    source_case("bounded-greedy", "(a){1,3}", "aaa", match(0, 3, (2, 3)))
    source_case("bounded-lazy", "(a){1,3}?", "aaa", match(0, 1, (0, 1)))
    source_case("atleast-one-is-plus", "(a|){1,}", "a", match(0, 1, (0, 1)))
    source_case("atleast-two-nullable", "(a|){2,}", "aa", match(0, 2, (1, 2)))
    source_case("atleast-required-empty", "(a|){2,}", "a", match(0, 1, (1, 1)))
    source_case("atleast-zero-is-star", "(a|){0,}", "aa", match(0, 2, (1, 2)))
    source_case("atleast-lazy", "(a|){1,}?", "aa", match(0, 1, (0, 1)))
    source_case("atleast-lazy-full", "(a|){1,}?", "aa", match(0, 2, (1, 2)), "full-match")
    source_case("unicode", "(λ)", "aλb", match(1, 3, (1, 3)))
    source_case("spaces-are-data", "a λ", "_a λ_", match(1, 5))
    source_case("escaped-class-punctuation", r"[\&\&]", "&", match(0, 1))
    add("nul-is-data", lets([
        ("$source", form("str:from-codepoints", "(0)")),
        ("$input", form("str:from-codepoints", "(97 0 98)")),
        ("$p", form("re:compile", "$source")),
        ("$r", form("re:find", "$p", "$input")),
        ("$c", form("re:close", "$p")),
    ], "$r"), match(1, 2))
    source_case("dot-excludes-newline", ".", "\n", Symbol("re:no-match"))
    source_case("dotall", "(?s:.)", "\n", match(0, 1))
    source_case("multiline", "(?m)^b$", "a\nb\nc", match(2, 3))
    source_case("class", "[a-c]+", "xabc", match(1, 4))
    source_case("complement", "[^a-c]+", "abcλ\n", match(3, 6))
    source_case("overlapping-complement", "[^b-da-c]+", "abcdλ", match(4, 6))
    source_case("complement-negative", "[^a-c]", "abc", Symbol("re:no-match"))
    source_case("complete-negative", "a+", "bbb", Symbol("re:no-match"))
    source_case("empty-iteration", "", "aλ", node("re:matches", list_value([
        match(0, 0), match(1, 1), match(3, 3)])), "find-all")
    source_case("adjacent-empty", "a*", "aa", node("re:matches", list_value([match(0, 2)])), "find-all")

    for pattern in ("(?<letter>λ)", "(?P<letter>λ)"):
        expected = node("re:match", 1, 3, list_value([node("re:capture-span", 0, 1, 3),
            node("re:named-group", StringLiteral("letter"), node("re:capture-span", 1, 1, 3))]))
        source_case("named-" + str(len(queries)), pattern, "aλb", expected)
    add("syntax-negative", form("re:compile", text("(a")), predicate=lambda x:
        headed(x, "re:source-observation", 1) and packed(x[1], "ParserRejectedV1"))
    add("reversed-range", form("re:compile", text("[z-a]")), predicate=lambda x: headed(x, "re:source-fault", 3))
    source_case("lookahead", "(?=a)a", "a", match(0, 1), oracle=False)
    add("missing-class-interpretation", form("re:compile", text(r"\p{MissingProperty}")),
        predicate=lambda x: headed(x, "re:interpretation-needed", 4) and packed(x[4], "ParserAcceptedV1", 1))
    add("count-beyond-native-index", form("re:compile", text("a{4294967296}")),
        predicate=lambda x: headed(x, "re:interpretation-needed", 4) and packed(x[4], "ParserAcceptedV1", 1))
    source_case("class-intersection-empty", "[a&&b]", "ab", Symbol("re:no-match"), oracle=False)
    source_case("class-symmetric-difference", "[a~~b]", "b", match(0, 1), oracle=False)
    source_case("class-difference", "[a-z--aeiou]+", "a bcdf e", match(2, 6), oracle=False)
    source_case("class-fold-before-difference", "(?i)[a--A]", "aA", Symbol("re:no-match"), oracle=False)
    for name, pattern, input_, expected in (
            ("class-nested-difference", "[a-z--[aeiou]]+", "a bcdf e", match(2, 6)),
            ("class-greek-letters", r"[\p{Greek}&&\p{Letter}]+", "aλΣ7", match(1, 5)),
            ("class-adjacency-before-intersection", "[ab&&bc]", "a", Symbol("re:no-match")),
            ("class-set-operators-associate-left", "[a--b~~c]", "c", match(0, 1)),
            ("class-single-ampersand", "[a&b]", "&", match(0, 1)),
            ("class-single-tilde", "[a~b]", "~", match(0, 1)),
            ("class-historical-open-bracket", "[[]", "[", match(0, 1)),
            ("class-nested-empty-does-not-invent", "[[]]", "[", Symbol("re:no-match")),
            ("class-punctuation-endpoints", "[&-~]", "~", match(0, 1)),
            ("class-opening-bracket-endpoint", "[[-a]", "_", match(0, 1)),
            ("class-escaped-endpoints", r"[\x{41}-\u{5A}]", "Z", match(0, 1)),
            ("class-nested-negation-fold-keeps-other", "(?i)[a-z&&[^a]]", "B", match(0, 1)),
            ("class-nested-negation-fold-rejects-equivalent", "(?i)[a-z&&[^a]]", "A", Symbol("re:no-match")),
            ("class-x-nested-internal-trivia", "(?x)[[a] # ] b", "#b", match(0, 2)),
            ("class-x-nested-empty-prefix", "(?x)[[]] # comment\n b", "[b", Symbol("re:no-match")),
            ("class-x-literal-open-prefix", "(?x)[[] # comment\n b", "[b", match(0, 2)),
            ("class-x-longest-lexical-region", "(?x)[[] # ] # comment\n b", "#b", match(0, 2)),
            ("class-x-escaped-closing-bracket", r"(?x)[\] # ] b", "]b", match(0, 2))):
        source_case(name, pattern, input_, expected, oracle=False)
    for name, pattern in (("class-reversed-escaped-range", r"[\x{5A}-\x{41}]"),
                          ("class-set-escape-not-endpoint", r"[\d-z]")):
        add(name, form("re:compile", text(pattern)), predicate=lambda x: headed(x, "re:source-fault", 3))
    for name, pattern, input_, expected in (
            ("lookahead-context", "name(?=:)", "name:", match(0, 4)),
            ("negative-lookahead", "cat(?!s)", "cat!", match(0, 3)),
            ("negative-lookahead-reject", "cat(?!s)", "cats", Symbol("re:no-match")),
            ("lookbehind", "(?<=ID:)[0-9]+", "ID:123", match(3, 6)),
            ("negative-lookbehind", "(?<!ID:)[0-9]+", "xx123", match(2, 5)),
            ("capture-atomic-positive", "(?=(a+))a", "aaa", match(0, 1, (0, 3))),
            ("capture-negative-isolation", "(?!(a))b", "b", match(0, 1, None)),
            ("backref-positive", r"^([a-z]+)-\1$", "cat-cat", match(0, 7, (0, 3))),
            ("backref-negative", r"^([a-z]+)-\1$", "cat-dog", Symbol("re:no-match")),
            ("backref-empty", r"()\1", "", match(0, 0, (0, 0))),
            ("backref-unmatched", r"(a)?\1", "", Symbol("re:no-match")),
            ("backref-open", r"(a\1)", "aa", Symbol("re:no-match")),
            ("backref-simple-fold", r"(?i)(K)\1", "KK", match(0, 4, (0, 1))),
            ("backref-mixed-decimal", "(a)\\١", "aa", match(0, 2, (0, 1))),
            ("lookbehind-forward-priority", "(?<=([ab]+)([bc]+))$", "abc", match(3, 3, (0, 2), (2, 3)))):
        source_case(name, pattern, input_, expected, oracle=False)
    add("reverse-lookbehind-policy", lets([
        ("$p", form("re:compile-with", text("(?<=([ab]+)([bc]+))$"), "(re:lookbehind-policy Reverse)")),
        ("$r", form("re:find", "$p", text("abc"))),
        ("$c", form("re:close", "$p")),
    ], "$r"), match(3, 3, (0, 1), (1, 3)))
    for name, pattern, input_, expected in (
            ("atomic-no-body-backtracking", r"(?=(a|aa))\1$", "aa", match(1, 2, (1, 2))),
            ("original-context-lookbehind-anchor", "(?<=^ab)c", "xabc", Symbol("re:no-match")),
            ("backref-ascii-fold", r"(?i-u)(K)\1", "Kk", match(0, 2, (0, 1))),
            ("backref-ascii-not-unicode-fold", r"(?i-u)(K)\1", "KK", Symbol("re:no-match")),
            ("forward-reference-unmatched", r"\1(a)", "aa", Symbol("re:no-match")),
            ("backref-maximal-numeric", r"(a)\12", "aa2", None)):
        if expected is None:
            add(name, form("re:compile", text(pattern)), predicate=lambda x: headed(x, "re:interpretation-needed", 2))
        else:
            source_case(name, pattern, input_, expected, oracle=False)
    for spelling in (r"(?<word>a+)\k<word>", r"(?P<word>a+)(?P=word)"):
        expected = node("re:match", 0, 4, list_value([node("re:capture-span", 0, 0, 4),
            node("re:named-group", StringLiteral("word"), node("re:capture-span", 1, 0, 2))]))
        source_case("named-backref-" + str(len(queries)), spelling, "aaaa", expected, "full-match", oracle=False)
    for spelling, expected in ((r"(λ)\1(?=!)", match(1, 5, (1, 3))),
                              ("(?<=([ab]+)([bc]+))$", match(3, 3, (0, 1), (1, 3)))):
        add("extended-artifact-" + str(len(queries)), lets([
            ("$p", form("re:compile-with", text(spelling), "(re:lookbehind-policy Reverse)")),
            ("$bytes", form("re:store", "$p")),
            ("$closed", form("re:close", "$p")),
            ("$q", form("re:restore", "$bytes")),
            ("$r", form("re:find", "$q", text("aλλ!" if "λ" in spelling else "abc"))),
            ("$c", form("re:close", "$q")),
        ], "$r"), expected)
    add("negative-look-cutoff-not-absence", lets([
        ("$p", form("re:compile", text("(?!(a+))b"))),
        ("$r", form("re:find-bounded", "$p", text("bbb"), "1")),
        ("$c", form("re:close", "$p")),
    ], "$r"), predicate=lambda x: headed(x, "re:incomplete", 1))
    for name, value, input_, expected in (
            ("nullary", "(re:epsilon)", "a", match(0, 0)),
            ("assertion-string", '(re:seq (re:assert "text-start") (re:literal "97"))', "a", match(0, 1)),
            ("assertion-symbol", '(re:seq (re:assert text-start) (re:literal 97))', "a", match(0, 1)),
            ("elided-constructed", '(re:repeat-exact (re:capture 1 (re:literal 97)) re:count-zero)', "", match(0, 0, None)),
            ("nested-elided-constructed", '(re:repeat-exact (re:capture 1 (re:repeat-exact (re:capture 2 (re:literal 97)) re:count-zero)) re:count-zero)', "", match(0, 0, None, None))):
        add("value-adapter-" + name, lets([
            ("$p", form("re:compile-value", value)),
            ("$r", form("re:find", "$p", text(input_))),
            ("$c", form("re:close", "$p")),
        ], "$r"), expected)
    for name, value in (("unknown-assertion", '(re:assert "unknown")'),
                        ("noncanonical-scalar", '(re:literal "097")'),
                        ("outside-unicode-profile", '(re:literal 1114112)'),
                        ("reserved-group-layout", '(re:capture 0 (re:literal 97))')):
        add("value-obligation-" + name, form("re:compile-value", value),
            predicate=lambda x: headed(x, "re:interpretation-needed", 2))

    for name, source, allowance, observation in (
            ("positive-a", "a", 2000, "ParserAcceptedV1"),
            ("positive-unicode", "λ", 2000, "ParserAcceptedV1"),
            ("negative", "(", 2000, "ParserRejectedV1"),
            ("incomplete", "a", 1, "ParserIncompleteV1")):
        add("packed-parser-" + name, lets([
            ("$p", form("langdef:load-baked-parser", text("regex-pattern-reader"))),
            ("$r", form("langdef:parser-parse-text-forest", "$p", text(source), str(allowance))),
            ("$c", form("langdef:close-parser", "$p")),
        ], "$r"), predicate=lambda x, observation=observation:
            headed(x, observation, 3) if observation == "ParserIncompleteV1" else packed(x, observation))
    ambiguous, profile, _, _ = parts("RegexAmbiguityControl", [rule("A", "a"), rule("B", "a")])
    add("packed-parser-ambiguity", lets([
        ("$p", form("langdef:load-authored-parser-values", ambiguous, profile)),
        ("$r", form("langdef:parser-parse-text-forest", "$p", text("a"), "2000")),
        ("$c", form("langdef:close-parser", "$p")),
    ], "$r"), predicate=lambda x: packed(x, "ParserAcceptedV1", 2))
    add("bounded-work", lets([
        ("$p", form("re:compile", text("a+"))),
        ("$r", form("re:find-bounded", "$p", text("aaa"), "1")),
        ("$closed", form("re:close", "$p")),
    ], "$r"), predicate=lambda x: headed(x, "re:incomplete", 1))
    add("owner-close", lets([
        ("$p", form("re:compile", text("(?<letter>a)"))),
        ("$bytes", form("re:store", "$p")),
        ("$closed", form("re:close", "$p")),
        ("$q", form("re:restore", "$bytes")),
        ("$r", form("re:find", "$q", text("a"))),
        ("$c", form("re:close", "$q")),
        ("$stale", form("re:find", "$p", text("a"))),
    ], "(RegexOwnersV1 $r $stale)"), predicate=lambda x:
        headed(x, "RegexOwnersV1", 2) and headed(x[1], "re:match", 3) and
        x[1][3][2][1][1] == StringLiteral("letter") and headed(x[2], "re:failed", 2) and
        x[2][1] == Symbol("re:invalid-handle"))
    add("bad-artifact", form("re:restore", text("not a program")),
        predicate=lambda x: headed(x, "re:failed", 2) and x[1] == Symbol("re:artifact"))

    def render_case(name, pattern, input_, operation, expected=None, replacement=None, predicate=None):
        operands = ["$p", text(input_)] + ([] if replacement is None else [replacement])
        add(name, lets([
            ("$p", form("re:compile", text(pattern))),
            ("$r", form("re:" + operation, *operands)),
            ("$c", form("re:close", "$p")),
        ], "$r"), expected, predicate)

    render_case("split-words", " +", "a  b c", "split",
        node("re:pieces", list_value([StringLiteral(x) for x in ("a", "b", "c")])))
    render_case("split-empty-unicode", "", "aλ", "split",
        node("re:pieces", list_value([StringLiteral(x) for x in ("", "a", "λ", "")])))
    render_case("split-no-match", "z", "abc", "split",
        node("re:pieces", list_value([StringLiteral("abc")])))
    render_case("replace-shared-empty-rule", "a*", "baaa", "replace-literal", StringLiteral("<>b<>"), text("<>"))
    render_case("replace-literal-dollar", "a", "aba", "replace-literal", StringLiteral("$1b$1"), text("$1"))
    render_case("replace-unicode", "λ", "aλbλ", "replace-literal", StringLiteral("a!b!"), text("!"))
    render_case("replace-no-match", "z", "abc", "replace-literal", StringLiteral("abc"), text("!"))
    render_case("replace-selected-captures", "(?<x>a+)", "baa", "replace-template", StringLiteral("b<aa>"),
        '(LCons (re:text "<") (LCons (re:group "x") (LCons (re:text ">") LNil)))')
    render_case("replace-unmatched", "(a)?b", "b", "replace-template", StringLiteral("<>"),
        '(LCons (re:text "<") (LCons (re:group 1) (LCons (re:text ">") LNil)))')
    render_case("replace-template-invalid", "a", "a", "replace-template", replacement='(LCons (re:group 3) LNil)',
        predicate=lambda x: headed(x, "re:failed", 2) and x[1] == Symbol("re:template"))

    # These observations use this library's declared Unicode/hex profile, not
    # another engine's syntax or Unicode version as an implicit specification.
    unicode_cases = (
        ("unicode-decimals", r"\d+", "x1٢３y", match(1, 7)),
        ("supplementary-decimal", r"\d", "𝟙", match(0, 4)),
        ("unicode-negative-decimals", r"\D+", "1λ٢", match(1, 3)),
        ("mixed-decimal-count", "a{١２}", "a" * 12, match(0, 12)),
        ("mixed-decimal-lower-upper", "a{１,٣}", "aaa", match(0, 3)),
        ("script-property", r"\p{Greek}+", "aλΣb", match(1, 5)),
        ("script-alias", r"\p{sc=Grek}+", "aλΣb", match(1, 5)),
        ("category-property", r"\p{General_Category=Decimal_Number}+", "x1٢３", match(1, 7)),
        ("binary-property", r"\p{Alphabetic}+", "1λa2", match(1, 4)),
        ("negative-property", r"\P{Greek}+", "λabΣ", match(2, 4)),
        ("known-property-empty-in-scalars", r"\p{Cs}", "λ", Symbol("re:no-match")),
        ("known-empty-scalar-complement", r"\P{Cs}", "λ", match(0, 2)),
        ("class-property", r"[\p{Greek}0-9]+", "xλ7Σ", match(1, 6)),
        ("class-negative-property", r"[\P{Greek}]+", "λabΣ", match(2, 4)),
        ("word-with-combining", r"\w+", "!λ́_7!", match(1, 7)),
        ("unicode-white-space", r"\s+", "a\u2003\u00a0b", match(1, 6)),
        ("word-boundary-unicode", r"\bλ\b", " λ ", match(1, 3)),
        ("ascii-word-declines-lambda", r"(?-u:\w+)", "λ", Symbol("re:no-match")),
        ("ascii-decimal-declines-arabic", r"(?-u:\d+)", "٢", Symbol("re:no-match")),
        ("ascii-boundary-original-context", r"(?-u:\ba\b)", "λaλ", match(2, 3)),
        ("unicode-boundary-original-context", r"\ba\b", "λaλ", Symbol("re:no-match")),
        ("unicode-simple-fold-kelvin", "(?i)k", "K", match(0, 3)),
        ("unicode-simple-fold-sigma", "(?i)σ", "ς", match(0, 2)),
        ("folded-negation-before-complement", "(?i)[^a]", "A", Symbol("re:no-match")),
        ("folded-negation-keeps-other", "(?i)[^a]", "λ", match(0, 2)),
        ("folding-is-not-full-expansion", "(?i)ß", "ss", Symbol("re:no-match")),
        ("ascii-fold-is-not-unicode-fold", "(?i-u:k)", "K", Symbol("re:no-match")),
        ("ascii-fold-keeps-uppercase", "(?i-u:k)", "K", match(0, 1)),
        ("scoped-fold-restored", "(?i:a)b", "AB", Symbol("re:no-match")),
        ("hex-supplementary", r"\x{1F680}", "a🚀b", match(1, 5)),
        ("unicode-hex", r"\u{3BB}", "aλb", match(1, 3)),
        ("hex-in-class", r"[\x{3BB}]+", "aλλb", match(1, 5)),
        ("extended-mode", "(?x) a # note\n λ", "aλ", match(0, 3)),
        ("extended-unicode-whitespace", "(?x)a\u2003λ", "aλ", match(0, 3)),
        ("extended-class-and-escapes", r"(?x)[ #]\#\ ", "## ", match(0, 3)),
        ("scoped-extended-restored", "(?x:a b)c d", "abc d", match(0, 5)),
        ("extended-disabled", "(?x)a(?-x: )b", "a b", match(0, 3)),
        ("spaced-flag-disable", "(?x)( ? - x ) a b", " a b", match(0, 4)),
        ("extended-missing-space", "(?x)a b", "a b", Symbol("re:no-match")),
    )
    for name, pattern, input_, expected in unicode_cases:
        source_case(name, pattern, input_, expected, oracle=False)
    add("class-backspace", lets([
        ("$p", form("re:compile", text(r"[\b]"))),
        ("$input", form("str:from-codepoints", "(97 8 98)")),
        ("$r", form("re:find", "$p", "$input")),
        ("$c", form("re:close", "$p")),
    ], "$r"), match(1, 2))
    for name, source_ in (("hex-surrogate", r"\x{D800}"),
                          ("hex-outside-scalars", r"\x{110000}")):
        add(name, form("re:compile", text(source_)),
            predicate=lambda x: headed(x, "re:source-fault", 3))
    for name, source_ in (("non-decimal-numeral", "a{²}"),
                          ("decimal-is-not-hex", r"\x{١}")):
        add(name, form("re:compile", text(source_)),
            predicate=lambda x: headed(x, "re:source-observation", 1) and packed(x[1], "ParserRejectedV1"))

    for name, pattern, input_, replacement, expected in (
            ("template-number", "(λ)", "aλb", "<$١>", "a<λ>b"),
            ("template-name", "(?<名>λ)", "λ", "${名}", "λ"),
            ("template-mixed-nd", "(a)(b)", "ab", "${٠２}:$٢:$1:$$:$0", "b:b:a:$:ab"),
            ("template-unmatched-empty", "(a)?b", "b", "<$1>", "<>"),
            ("template-original-bytes", "(?i)(k)", "K", "<$1>", "<K>"),
            ("template-empty-cursor", "", "aλ", "$$", "$a$λ$")):
        render_case(name, pattern, input_, "replace-template", StringLiteral(expected), text(replacement))
    for name, replacement, reason in (
            ("template-unknown-name", "${missing}", "re:template-reference"),
            ("template-longest-number", "$12", "re:template-reference"),
            ("template-missing-brace", "${1", "re:template"),
            ("template-empty-reference", "${}", "re:template"),
            ("template-trailing-dollar", "$", "re:template")):
        render_case(name, "(a)", "a", "replace-template", replacement=text(replacement),
            predicate=lambda x, reason=reason: headed(x, "re:failed", 2) and x[1] == Symbol(reason))
    add("unicode-artifact-independent-owner", lets([
        ("$p", form("re:compile", text(r"(?i)(\bκ\w*)"))),
        ("$bytes", form("re:store", "$p")),
        ("$closed", form("re:close", "$p")),
        ("$q", form("re:restore", "$bytes")),
        ("$r", form("re:captures", "$q", text(" Κλ "))),
        ("$c", form("re:close", "$q")),
    ], "$r"), match(1, 5, (1, 5)))
    for name, pattern, input_, expected in (
            ("ascii-boundary-artifact", r"(?-u:\ba\b)", "λaλ", match(2, 3)),
            ("ascii-nonboundary-artifact", r"(?-u:\Bλ)", "λ", match(0, 2)),
            ("unicode-boundary-artifact-negative", r"\ba\b", "λaλ", Symbol("re:no-match"))):
        add(name, lets([
            ("$p", form("re:compile", text(pattern))),
            ("$bytes", form("re:store", "$p")),
            ("$closed", form("re:close", "$p")),
            ("$q", form("re:restore", "$bytes")),
            ("$r", form("re:find", "$q", text(input_))),
            ("$c", form("re:close", "$q")),
        ], "$r"), expected)

    for name, theory in (("RegexSelectionCompiler", "RegexSelectionCompilerV1"), ("RegexSyntax", "PatternText"),
                        ("RegexClass", "RegexClassAlgebraV1"), ("RegexExtendedCompiler", "RegexExtendedCompilerV1")):
        add("module-exact-" + name, lets([
            ("$reader", form("langdef:load-baked-parser", text("module-core-reader"))),
            ("$source", form("module:read-file", "$reader", text("source"), text(str(ROOT / f"langdef/regex/{name}.module")))),
            ("$assembled", form("module:assemble", "$source", text(theory), "LNil", "LNil", "OriginViewV1", text("regex:1"))),
            ("$core", form("module:core", "$assembled", text(theory))),
            ("$portable", form("module:read-file", "ModulePortableV1", text("portable"),
                text(str(ROOT / (f"langdef/regex/{name}.portable.module" if name in ("RegexClass", "RegexExtendedCompiler") else f"tests/regex/{name}.portable.module"))))),
            ("$reference", form("module:core", "$portable", text("core"))),
            ("$closed", form("langdef:close-parser", "$reader")),
        ], "(RegexCoreAgreementV1 (== $core $reference) $core $reference)"), predicate=lambda x:
            headed(x, "RegexCoreAgreementV1", 3) and x[1] in (Symbol("True"), Symbol("true")) and x[2] == x[3])

    for name, value, admitted in (
            ("typed-class-union", '(re:class-union (re:class-empty) (re:class-range 97 122))', True),
            ("typed-class-fold", '(re:class-complement (re:class-fold (re:class-property "Alphabetic")))', True),
            ("class-scalar-is-not-string", '(re:class-range "97" "122")', False)):
        add(name, lets([
            ("$portable", form("module:read-file", "ModulePortableV1", text("portable"),
                text(str(ROOT / "langdef/regex/RegexClass.portable.module")))),
            ("$core", form("module:core", "$portable", text("core"))),
            ("$authority", form("langdef:load-language-value", "$core")),
            ("$r", form("langdef:admit-term-data", "$authority", text("RegexClass"), value)),
            ("$closed", form("langdef:close-language", "$authority")),
        ], "$r"), predicate=lambda x, admitted=admitted:
            headed(x, "LangDef:TermAdmitted", 0) if admitted else headed(x, "LangDef:TermRejected", 2))

    for name, value in (("scalar97", '(re:literal "97")'), ("scalar0", '(re:literal "0")'),
                        ("not-raw-integer", '(re:literal 97)')):
        add("core-payload-" + name, lets([
            ("$portable", form("module:read-file", "ModulePortableV1", text("portable"),
                text(str(ROOT / "tests/regex/RegexSelectionCompiler.portable.module")))),
            ("$core", form("module:core", "$portable", text("core"))),
            ("$authority", form("langdef:load-language-value", "$core")),
            ("$r", form("langdef:admit-term-data", "$authority", text("ReValue"), value)),
            ("$closed", form("langdef:close-language", "$authority")),
        ], "$r"), predicate=lambda x, negative=name == "not-raw-integer":
            (headed(x, "LangDef:TermAdmitted", 0) if not negative else
             headed(x, "LangDef:TermRejected", 2)))

    count = oracle_count = difference_count = example_count = 0
    # Keep complete source group identities even when code emission removes a
    # trailing group. This named observation differs from Rust's HIR layout;
    # do not normalize away arbitrary differential mismatches.
    rust_elided_layouts = {
        "elided-group": dict(outcome="match", groups=[[0, 0]]),
        "elided-suffix": dict(outcome="match", groups=[[0, 1], [0, 1]]),
    }
    for host in ("he", "petta", "petta-1.1", "prime"):
        library = ROOT / ("lib/petta/lib_module.metta" if host.startswith("petta") else "lib/lib_module.metta")
        program = "!(import! &self re)\n!(import! &self str)\n!(import! &self " + text(str(library)) + ")\n" + "\n".join(queries) + "\n"
        values = run(args.cetta.resolve(), host, args.output.resolve(), "workflow", program)
        results = {value[1].text: value[2] for value in values if headed(value, "RegexWorkflowCaseV1", 2)}
        if set(results) != set(predicates):
            raise AssertionError((host, "missing/extra results", set(results) ^ set(predicates)))
        for name, predicate in predicates.items():
            if not predicate(results[name]):
                raise AssertionError((host, name, results[name]))
            count += 1
        if args.oracle:
            def groups(value):
                answer, rows = [], value[3]
                while headed(rows, "LCons", 2):
                    entry, rows = rows[1:]
                    if headed(entry, "re:named-group", 2):
                        entry = entry[2]
                    answer.append(None if headed(entry, "re:unmatched", 1) else list(entry[2:4]))
                return answer

            for name, (pattern, input_, mode) in oracle_cases.items():
                value = results[name]
                if mode == "all":
                    matches, rows = [], value[1]
                    while headed(rows, "LCons", 2):
                        matches.append(groups(rows[1])); rows = rows[2]
                    actual = dict(matches=matches)
                else:
                    found = headed(value, "re:match", 3)
                    actual = dict(outcome="match" if found else "no-match", groups=groups(value) if found else [])
                expected = observe(args.oracle.resolve(), [mode, pattern, input_])
                if actual != expected:
                    if name not in rust_elided_layouts or expected != rust_elided_layouts[name]:
                        raise AssertionError((host, name, "source oracle mismatch", expected, actual))
                    # Actual was already checked against its independent full
                    # layout above. Only these two explicit divergences pass.
                    difference_count += 1
                    continue
                oracle_count += 1
        example_count += check_examples(args.cetta.resolve(), host, args.output.resolve())
    print(f"(RegexWorkflowV1Summary {count} 0)")
    print(f"(RegexExamplesV1Summary {example_count} 0)")
    if args.oracle:
        print(f"(RegexSourceDifferentialV1Summary {oracle_count} 0)")
        print(f"(RegexRetainedElidedLayoutDifferenceV1 {difference_count})")


if __name__ == "__main__":
    main()
