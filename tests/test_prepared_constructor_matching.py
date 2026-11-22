"""Check constructor unification and captured branch bindings across workers."""

import argparse
from pathlib import Path
import tempfile

from test_prepared_held_syntax import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args()
    binary = args.binary.resolve()
    routes = [
        ("registers", {}),
        ("nodes", {"CETTA_PREPARED_PURE_CODE_REFERENCE": "1"}),
        ("canonical", {"CETTA_PREPARED_PURE_REFERENCE": "1",
                       "CETTA_OPEN_EQUATIONS_REFERENCE": "1"}),
        ("collecting", {"CETTA_GC": "1", "CETTA_GC_BUDGET_MB": "1"}),
    ]
    checks = 0
    for name in ("prepared_constructor_matching", "prepared_constructor_loop", "prepared_constructor_hosts"):
        source = Path("tests/petta") / (name + ".metta")
        expected = source.with_suffix(".expected").read_text().rstrip()
        for profile in (None, "extended"):
            for route, flags in routes:
                actual, counters = run(binary, source, profile, flags, args.stats)
                assert actual.rstrip() == expected, (name, profile, route, expected, actual)
                if args.stats and name == "prepared_constructor_loop":
                    commits = counters["prepared-pure-call-commit"]
                    entries = counters["prepared-pure-call-tail-reentry"]
                    if route == "canonical":
                        assert commits == 0 and entries == 0, (profile, route)
                    else:
                        assert commits > 0 and entries >= 1000, (profile, route)
                        assert counters["prepared-pure-call-decline"] == 0, (profile, route)
                checks += 1
    if args.stats:
        # Check native coverage separately: equality with a reference run
        # alone could hide a constructor path that always falls back.
        fixture = Path("tests/petta/prepared_constructor_matching.metta").read_text().splitlines()
        definitions = fixture[0] + "\n" + "\n".join(
            line for line in fixture[1:] if not line.startswith("!")) + "\n"
        cases = [
            ("(pc:sum 5)", "7", True),
            ("(pc:whole 5)", "(Pair 5 2)", True),
            ("(pc:closed-constructor 5)", "yes", True),
            ("(pc:closed-constructor 6)", "no", True),
            ("(pc:empty-constructor ())", "yes", True),
            ("(pc:empty-constructor (Other 1))", "no", True),
            ("(pc:nested 5)", "7", True),
            ("(pc:result 5)", "(Result 5 2 7)", True),
            ("(pc:forward-site 5)", "8", True),
            ("(pc:scoped-site 5)", "8", True),
            ("(pc:mismatch 5)", "17", True),
            ("(pc:failure-binds 5)", "23", True),
            ("(pc:atom-field 0)", "label", True),
            ("(pc:compound-field 7)", "(Tree 7 (Leaf 2))", True),
            ("(let $value (pc:dag-read 12) (get-metatype $value))", "Expression", True),
            ("(pc:repeat 7)", "7", False),
            ("(pc:repeat-fail 7)", "-1", False),
            ("(pc:prebound 7)", "3", False),
            ("(pc:two-sided)", "(Aliases (Joined 2 1) 2 1)", False),
            ("(pc:escape 7)", "(After 10 7 3)", False),
            ("(pc:cons-case)", "(Fields 1 (2))", False),
            ("(pc:code-field 7)", "9", False),
        ]
        with tempfile.TemporaryDirectory(prefix="constructor-cases-", dir="runtime") as tmp:
            source = Path(tmp) / "case.metta"
            for expression, value, compiled in cases:
                source.write_text(definitions + "!" + expression + "\n")
                actual, counters = run(binary, source, None, {}, True)
                assert actual.rstrip() == "true\n" + value, (expression, actual)
                commits = counters["prepared-pure-call-commit"]
                declines = counters["prepared-pure-call-decline"]
                assert (commits > 0 if compiled else commits == 0), expression
                assert (declines == 0 if compiled else declines > 0), expression
                checks += 1
    print(f"PASS: constructor matches preserve captures, failure, aliases and effects ({checks} cases)")


if __name__ == "__main__":
    main()
