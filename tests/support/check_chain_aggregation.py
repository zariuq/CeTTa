#!/usr/bin/env python3
"""Check long-path counts/sums against occurrence enumeration and ordinary folds."""
import argparse
import json
import os
from pathlib import Path
import random
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] /
                       "benchmarks/mam_families"))
from paths import Row, declarations, enumerate_values, fold, pattern


def counters(result):
    return {parts[1]: int(parts[2])
            for line in result.stderr.splitlines()
            if len(parts := line.split()) == 3 and
            parts[0] == "runtime-counter"}


def run(binary, language, path, reference, stats, *, profile="extended",
        options=(), settings=None, allow_incomplete=False):
    env = dict(os.environ)
    env.pop("CETTA_FOLD_ARITH_REFERENCE", None)
    env.pop("CETTA_CONJUNCTION_COUNT_REFERENCE", None)
    env.pop("CETTA_RELATIONAL_COUNT_REFERENCE", None)
    if reference:
        env["CETTA_FOLD_ARITH_REFERENCE"] = "1"
        env["CETTA_CONJUNCTION_COUNT_REFERENCE"] = "1"
        env["CETTA_RELATIONAL_COUNT_REFERENCE"] = "1"
    env.update(settings or {})
    command = [str(binary), "--lang", language, "--profile", profile]
    if stats:
        command.append("--emit-runtime-stats")
    result = subprocess.run(command + list(options) + [str(path)], env=env, capture_output=True,
                            text=True, timeout=180)
    if not allow_incomplete:
        assert result.returncode == 0, (language, path.name, result.stderr[-2000:])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args()
    binary = args.binary.resolve()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    rng = random.Random(8917)
    cases = []
    for levels in (4, 5, 8, 12):
        rows = [[Row(rng.randrange(3), rng.randrange(3), rng.randrange(-7, 8))
                 for _ in range(5)] for _ in range(levels)]
        for relation in rows:
            relation.append(relation[0])
        column = levels // 2
        values = enumerate_values(rows, column)
        cases.append((f"random-{levels}", rows, column, len(values), sum(values)))
        product = [[Row(0, 0, -3), Row(0, 0, 5)] for _ in range(levels)]
        values = enumerate_values(product, column)
        cases.append((f"product-{levels}", product, column, len(values), sum(values)))
    empty = [[Row(0, 0, 1)] for _ in range(5)]
    empty[2] = []
    cases.append(("missing-bridge", empty, 2, 0, 0))
    records = []
    for name, rows, column, count, total in cases:
        query = pattern(len(rows))
        for language in ("he", "petta"):
            text = declarations(rows)
            text += fold(language, query, "1") + fold(language, query, f"$v{column}")
            path = args.output_directory / f"{name}-{language}.metta"
            path.write_text(text)
            actual = run(binary, language, path, False, args.stats)
            reference = run(binary, language, path, True, False)
            expected = f"{count}\n{total}\n"
            if language == "he":
                expected = f"[{count}]\n[{total}]\n"
            assert actual.stdout == expected == reference.stdout, (
                name, language, expected, actual.stdout, reference.stdout)
            if args.stats and language == "petta":
                observed = counters(actual)
                assert observed.get("native-fold-run", 0) > 0, (name, observed)
                assert observed.get("native-fold-moments", 0) > 0, (name, observed)
            records.append({"case": name, "language": language,
                            "count": count, "sum": total})
    # A consumer that observes order cannot use unordered moments. Nor can
    # a path message erase the final key's correlation with the first key.
    rows = [[Row(0, 0, 2), Row(0, 0, 3)] for _ in range(4)]
    query = pattern(4)
    controls = {
        "ordered": (declarations(rows) +
            "(= (rolling $i $a) (% (+ (* 31 $a) $i) 1000003))\n" +
            fold("petta", query, "$v2", operation="rolling"), True),
        "cycle": (declarations(rows) +
            fold("petta", query.replace("$k4", "$k0"), "1"), False),
        "overflow": (declarations([[Row(0, 0, 9223372036854775807)] * 2
                                   for _ in range(4)]) + fold("petta", query, "$v2"), True),
        "caller-binding": (declarations(rows) +
            "!(let $k1 1 " + fold("petta", query, "1")[1:].strip() + ")\n", False),
        "template-effects": (declarations(rows) +
            "(= (observe $i) (chain (add-atom &self (seen $i)) $_ $i))\n" +
            fold("petta", query, "(observe $v2)") +
            "!(foldall + (match &self (seen $i) 1) 0)\n", True),
    }
    for name, (text, declined) in controls.items():
        path = args.output_directory / f"{name}-petta.metta"
        path.write_text(text)
        actual = run(binary, "petta", path, False, args.stats)
        reference = run(binary, "petta", path, True, False)
        assert actual.stdout == reference.stdout, (name, actual.stdout, reference.stdout)
        if args.stats and declined:
            assert "runtime-counter native-fold-declined " in actual.stderr, (name, actual.stderr)
        if name == "caller-binding":
            assert actual.stdout == "0\n", actual.stdout
        if name == "template-effects":
            assert actual.stdout == "40\n16\n", actual.stdout
        records.append({"case": name, "language": "petta", "ordinary_fold": True})

    # The count observer can cross an authored constructor or equation, but
    # its bindings, execution purse and template obligations still apply.
    count = f"(size-atom (collapse (match &self {query} 1)))"
    nested_fold = f"(answer {fold('petta', query, '2', initial=3)[1:].strip()})"
    contexts = {
        "count-root": (f"!{count}\n", "16\n", "path"),
        "count-field": (f"!(answer {count})\n", "(answer 16)\n", "path"),
        "count-fold-field": (f"!{nested_fold}\n", "(answer 35)\n", "path"),
        "count-bound-first": (
            f"(= (count-key $k0) (answer {count}))\n"
            "!(count-key 0)\n!(count-key 1)\n", "(answer 16)\n(answer 0)\n", "path"),
        "count-bound-separator": (
            f"!(let $k1 0 (answer {count}))\n", "(answer 16)\n", "relational"),
        "count-bound-list": (
            f"!(let $k0 (cons 0 ()) (answer {count}))\n", "(answer 0)\n", "ordinary"),
        "count-empty-template": (
            f"!(answer (size-atom (collapse (match &self {query} (empty)))))\n",
            "(answer 0)\n", "ordinary"),
        "count-effect-template": (
            "(= (observed $i) (chain (add-atom &self (seen $i)) $_ $i))\n"
            f"!(answer (size-atom (collapse (match &self {query} (observed 1)))))\n"
            "!(size-atom (collapse (match &self (seen $i) 1)))\n", "(answer 16)\n16\n", "ordinary"),
        "count-branching-template": (
            "(= (two-values) (superpose (1 2)))\n"
            f"!(answer (size-atom (collapse (match &self {query} (two-values)))))\n",
            "(answer 32)\n", "ordinary"),
    }
    for name, (suffix, expected, route) in contexts.items():
        path = args.output_directory / f"{name}-petta.metta"
        path.write_text(declarations(rows) + suffix)
        actual = run(binary, "petta", path, False, args.stats)
        reference = run(binary, "petta", path, True, False)
        assert actual.stdout == reference.stdout == expected, (
            name, actual.stdout, reference.stdout, expected)
        if args.stats:
            observed = counters(actual)
            expected_routes = {
                "petta-conjunction-count-fold": route != "ordinary",
                "match-path-count-admission": route == "path",
                "match-relational-count-admission": route == "relational",
            }
            for counter, admitted in expected_routes.items():
                assert (observed.get(counter, 0) > 0) == admitted, (
                    name, route, counter, observed.get(counter, 0))
        records.append({"case": name, "language": "petta", "count_route": route})

    purse_path = args.output_directory / "count-fold-field-petta.metta"
    for options, settings in [(("--fuel", str(fuel)), None) for fuel in (16, 64, 512)] + [
            ((), {"CETTA_PETTA_MACHINE_TRANSITION_LIMIT": str(limit)}) for limit in (16, 64)]:
        actual = run(binary, "petta", purse_path, False, args.stats,
                     options=options, settings=settings, allow_incomplete=True)
        reference = run(binary, "petta", purse_path, True, False,
                        options=options, settings=settings, allow_incomplete=True)
        assert (actual.returncode, actual.stdout) == (reference.returncode, reference.stdout), (
            options, settings, actual, reference)
        if args.stats:
            assert counters(actual).get("petta-conjunction-count-fold", 0) == 0
        records.append({"case": "count-finite-purse", "options": options, "settings": settings})

    # HE's collection cardinality reaches the same backend operation; its
    # general fold retains the distinct dialect interpretation.
    he_count = f"(let $items (collapse (match &self {query} 1)) (size-atom $items))"
    for suffix, expected, admitted in [
            (f"!{he_count}\n", "[16]\n", True),
            (f"!(answer {he_count})\n", "[(answer 16)]\n", True),
            (f"!{count}\n", "[2]\n", False)]:
        path = args.output_directory / f"count-{len(records)}-he.metta"
        path.write_text(declarations(rows) + suffix)
        actual = run(binary, "he", path, False, args.stats)
        reference = run(binary, "he", path, True, False)
        assert actual.stdout == reference.stdout == expected, (
            path.name, actual.stdout, reference.stdout, expected)
        if args.stats:
            assert (counters(actual).get("match-path-count-admission", 0) > 0) == admitted
        records.append({"case": "count-context", "language": "he"})
    (args.output_directory / "results.json").write_text(json.dumps(records, indent=2) + "\n")
    print(f"PASS: {len(records)} long-path dialect and observation controls")


if __name__ == "__main__":
    main()
