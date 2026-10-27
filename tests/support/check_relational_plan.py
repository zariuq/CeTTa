#!/usr/bin/env python3
"""Check indexed counts through dialect, effect and continuation boundaries."""
import argparse
import json
from pathlib import Path

from check_chain_aggregation import counters, run
from joins import Factor, declarations, occurrence_join, query


def count_expression(language, pattern, template="1"):
    items = f"(collapse (match &self {pattern} {template}))"
    return f"(let $items {items} (size-atom $items))" if language == "he" \
        else f"(size-atom {items})"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args()
    binary = args.binary.resolve()
    out = args.output_directory
    out.mkdir(parents=True, exist_ok=True)
    pairs = ((0, 0), (0, 1), (1, 0), (1, 1)) * 2
    factors = (Factor("r", ("x", "y"), pairs), Factor("s", ("y", "z"), pairs),
               Factor("t", ("z", "x"), pairs))
    expected_count = sum(1 for _ in occurrence_join(factors))
    assert expected_count == 64
    pattern = query(factors)
    records = []
    for language in ("he", "petta"):
        expression = count_expression(language, pattern)
        cases = {
            "root": (f"!{expression}\n", True, str(expected_count)),
            "field": (f"!(answer {expression})\n", True, f"(answer {expected_count})"),
            "caller": (f"(= (count-key $x) (answer {expression}))\n"
                       "!(count-key 0)\n!(count-key 9)\n", None, None),
            "binding": (f"!(let $x 0 (answer {expression}))\n", None, None),
            "changes": (f"!{expression}\n!(add-atom &self (r 0 0 99))\n"
                        f"!{expression}\n!(remove-atom &self (r 0 0 99))\n!{expression}\n", True, None),
            "open-row": ("(r $a $b 99)\n" + f"!{expression}\n", True, None),
            "whole-variable-row": ("!(add-atom &self $whole)\n" + f"!{expression}\n", False, None),
            "numeric-equality": ("(r 0.0 0 99)\n" + f"!{expression}\n", False, None),
            "branching": ("(= (two-values) (superpose (1 2)))\n"
                          f"!{count_expression(language, pattern, '(two-values)')}\n", False, "128"),
            "effects": ("(= (observed $i) (chain (add-atom &self (seen $i)) $_ $i))\n"
                        f"!{count_expression(language, pattern, '(observed 1)')}\n", False, "64"),
            "witnesses": (f"!(match &self {pattern} (proof $o0 $o1 $o2))\n", False, None),
            "single-key-star": ("!" + count_expression(language,
                "(, (r $k $a $ra) (r $k $b $rb) (r $k $c $rc))") + "\n", True, "128"),
            "independent-alias": ("!" + count_expression(language,
                "(, (r $k $k $o) (s $a $b $p) (t $c $d $q))") + "\n", True, "256"),
            "binary-alias": ("!" + count_expression(language,
                "(, (r $k $k $o) (s $a $b $p))") + "\n", False, "32"),
            "covering-alias": ("!" + count_expression(language,
                "(, (r $x $x $o) (s $x $z $p) (t $z $x $q))") + "\n", True, "32"),
        }
        for name, (suffix, admitted, expected) in cases.items():
            path = out / f"{name}-{language}.metta"
            path.write_text(declarations(factors) + suffix)
            actual = run(binary, language, path, False, args.stats)
            reference = run(binary, language, path, True, False)
            assert actual.stdout == reference.stdout, (name, language, actual, reference)
            if expected is not None:
                text = f"[{expected}]\n" if language == "he" else f"{expected}\n"
                assert actual.stdout == text, (name, language, text, actual.stdout)
            observed = counters(actual)
            if args.stats and admitted is not None:
                assert (observed.get("match-relational-count-admission", 0) > 0) == admitted, (
                    name, language, observed)
            records.append({"case": name, "language": language, "expected_admission": admitted,
                            "observed": observed, "stdout": actual.stdout})
    # Binary relations, each row once, dense enough for rows of bits: the
    # counts agree with the reference and with the graph's own, and the
    # cyclic one is counted by rows of bits (an acyclic chain may be counted
    # by the flat chain count first).
    edges = sorted({(u, v) for u in range(8) for v in range(8)
                    if (u * 7 + v * 3) % 5 < 2})
    links = sorted({(u, v) for u in range(8) for v in range(8)
                    if (u + 2 * v) % 3 == 0})
    out_edges = {}
    for u, v in edges:
        out_edges.setdefault(u, set()).add(v)
    link_set = set(links)
    dense_cases = {
        "dense-triangle": ("(, (edge $x $y) (edge $y $z) (edge $z $x))",
                           sum(1 for x, y in edges for z in out_edges.get(y, ())
                               if (z, x) in set(edges)), True),
        "dense-chain": ("(, (edge $x $y) (link $y $z) (edge $z $w))",
                        sum(1 for x, y in edges for y2, z in links if y2 == y
                            for w in out_edges.get(z, ())), None),
        "dense-fixed": ("(, (edge 1 $y) (link $y $z) (edge $z $z))",
                        sum(1 for y in out_edges.get(1, ()) for y2, z in links
                            if y2 == y and (z, z) in set(edges)), None),
    }
    graph = "".join(f"(edge {u} {v})\n" for u, v in edges) + \
        "".join(f"(link {u} {v})\n" for u, v in links)
    for language in ("he", "petta"):
        for name, (pattern, expected, bits) in dense_cases.items():
            path = out / f"{name}-{language}.metta"
            path.write_text(graph + f"!{count_expression(language, pattern)}\n")
            actual = run(binary, language, path, False, args.stats)
            reference = run(binary, language, path, True, False)
            text = f"[{expected}]\n" if language == "he" else f"{expected}\n"
            assert actual.stdout == reference.stdout == text, (
                name, language, text, actual.stdout, reference.stdout)
            observed = counters(actual)
            if args.stats and bits:
                assert observed.get("dense-relation-admission", 0) > 0, (
                    name, language)
            records.append({"case": name, "language": language,
                            "observed": observed, "stdout": actual.stdout})
    purse_path = out / "field-petta.metta"
    for options, settings in [(("--fuel", str(fuel)), None) for fuel in (16, 64, 512)] + [
            ((), {"CETTA_PETTA_MACHINE_TRANSITION_LIMIT": str(limit)}) for limit in (16, 64)]:
        actual = run(binary, "petta", purse_path, False, args.stats,
                     options=options, settings=settings, allow_incomplete=True)
        reference = run(binary, "petta", purse_path, True, False,
                        options=options, settings=settings, allow_incomplete=True)
        assert (actual.returncode, actual.stdout) == (reference.returncode, reference.stdout), (
            options, settings, actual, reference)
        if args.stats:
            assert counters(actual).get("match-relational-count-admission", 0) == 0
        records.append({"case": "finite-purse", "options": options, "settings": settings})
    (out / "results.json").write_text(json.dumps(records, indent=2) + "\n")
    print(f"PASS: {len(records)} relational dialect, continuation and validity controls")


if __name__ == "__main__":
    main()
