#!/usr/bin/env python3
"""End-to-end observations of transform-native MeTTa configurations."""

import argparse
from collections import Counter
import os
from pathlib import Path
import subprocess


def bag(text):
    assert text.startswith("[") and text.endswith("]"), text
    items, start, depth, quoted, escaped = [], 1, 0, False, False
    for index, char in enumerate(text[1:-1], 1):
        if escaped:
            escaped = False
        elif char == "\\" and quoted:
            escaped = True
        elif char == '"':
            quoted = not quoted
        elif not quoted:
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            elif char == "," and depth == 0:
                items.append(text[start:index].strip())
                start = index + 1
    last = text[start:-1].strip()
    if last:
        items.append(last)
    return Counter(items)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./cetta")
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    fixtures = Path(__file__).resolve().parent
    count = 0

    def run(source, expected, profile="native", extra=(), incomplete=None, ordered=None,
            environment=None, trace=None):
        nonlocal count
        result = subprocess.run([binary, "--lang", "tmetta", "--profile", profile,
            *extra, "--expr", source], text=True, capture_output=True, timeout=90,
            env=dict(os.environ, **environment) if environment else None)
        if trace is not None:
            assert trace in result.stderr, (source, result.stderr)
        if incomplete:
            assert result.returncode != 0 and incomplete in result.stderr, result
            assert not result.stdout.strip(), result
        else:
            assert result.returncode == 0, result.stderr
            if ordered is not None:
                assert result.stdout.strip() == ordered, (source, result.stdout)
            observed = [bag(line) for line in result.stdout.splitlines() if line.strip()]
            assert observed == [Counter(values) for values in expected], (source, observed, expected, result.stderr)
        count += 1

    two = "(= (two) (superpose (0 1))) (= (dup $x) (Pair $x $x)) "
    run("!(+ 1 2)", [["3"]])
    run(two + "!(dup (two))", [["(Pair (two) (two))"]])
    run(two + "!(dup (two))", [["(Pair 0 0)", "(Pair 1 1)"]], profile="eager")
    run(two + "!(let $x (two) (Pair $x $x))", [["(Pair 0 0)", "(Pair 1 1)"]])
    pairs = [f"(Pair {a} {b})" for a in range(2) for b in range(2)]
    run(two + "!(let $x (two) (let $y (two) (Pair $x $y)))", [pairs])
    run("(= (old) (chain (add-atom &self (= (new-rule) 42)) $_ (new-rule))) "
        "!(old) !(new-rule)", [["42"], ["42"]])
    run(two + "!(let $code (hold (let $x (two) $x)) "
        "(let $a (run $code) (let $b (run $code) (Pair $a $b))))", [pairs])
    for profile in ("native", "eager"):
        run("(: raw (-> Atom Atom)) (= (raw $x) $x) !(raw (+ 1 2))", [["(+ 1 2)"]], profile)
        run("(: same-type (-> $T $T $T)) (= (same-type $a $b) $b) "
            "!(same-type (+ 1 2) (+ 2 3)) !(same-type 4 True)",
            [["5"], ["(Error True (TypeMismatch Number Bool))"]], profile)
        run("(: value-id (-> Number Number)) (= (value-id $x) $x) !(value-id (+ 1 2))", [["3"]], profile)
        run("(: raw-override (-> Number Atom)) (= (raw-override $x) $x) "
            "!(raw-override (hold (+ 1 2))) !(raw-override (run (hold True)))",
            [["(+ 1 2)"], ["True"]], profile)
        run("(= (held-query $outer) (let $x $outer "
            "(hold (match &s row (let $x (superpose (0 1)) $x))))) "
            "!(bind! &s (new-space)) !(add-atom &s row) "
            "!(let $query (held-query captured) (let $body (run $query) (run $body)))",
            [["()"], ["()"], ["0", "1"]], profile)
        run("!(let $x captured (let $body "
            "(car-atom (hold ((let $x (superpose (0 1)) $x) mark))) (run $body)))",
            [["0", "1"]], profile)
        run("!(count (collapse (superpose (a a b))))", [["3"]], profile)
        run("!(reflect (collapse (superpose (a b))))", [["a", "b"]], profile)
        run("!(count (collapse (collapse (superpose (a a b)))))", [["1"]], profile)
        run("!(reflect (collapse (amb outer "
            "(count (collapse (superpose (a b)))))))", [["outer", "2"]], profile)
        run("!(reflect (collapse (amb (count (collapse (superpose (a b)))) "
            "outer)))", [["outer", "2"]], profile)
        run("!(reflect (collapse (amb ready (collapse (if $open a b)))))",
            [], profile, incomplete="suspended")
        run("!(count (collapse (empty)))", [["0"]], profile)
        run("!(count (serialize canonical (reify (superpose (b a b)))))", [["3"]], profile)
        for permutation in ("b a b", "b b a", "a b b"):
            run(f"!(reflect (serialize canonical (reify (superpose ({permutation})))))",
                [["a", "b", "b"]], profile, ordered="[a, b, b]")
        for permutation in ("(Pair true 1) (Pair True 1)", "(Pair True 1) (Pair true 1)"):
            run(f"!(reflect (serialize canonical (reify (superpose ({permutation})))))",
                [["(Pair true 1)", "(Pair True 1)"]], profile,
                ordered="[(Pair true 1), (Pair True 1)]")
        for alternatives in ("(reify (superpose (z z))) (reify a)",
                             "(reify a) (reify (superpose (z z)))"):
            run(f"!(let $s (serialize canonical (reify (amb {alternatives}))) "
                "(let $a (count (car-atom $s)) (let $b (count (index-atom $s 1)) (Pair $a $b))))",
                [["(Pair 1 2)"]], profile)
        run("!(let $s (serialize canonical (reify (amb "
            "(reify (chain (unify $x 2) $_ b)) (reify (chain (unify $x 1) $_ a))))) "
            "(let $first (car-atom $s) (chain (reflect $first) $_ $x)))", [["1"]], profile)
        run("!(let $s (serialize canonical (reify (let $x 4 (reify (superpose ($x $x)))))) "
            "(reflect (car-atom $s)))", [["4", "4"]], profile)
        run("!(count (serialize any (reify (superpose (a b)))))", [["2", "2"]], profile)
        ordered_answers = "(serialize canonical (reify (superpose (b a b))))"
        run(f"!(car-atom {ordered_answers}) !(index-atom {ordered_answers} 1) "
            f"!(reflect (cdr-atom {ordered_answers})) !(size-atom (cdr-atom {ordered_answers}))",
            [["a"], ["b"], ["b", "b"], ["2"]], profile)
        run(f"!(list:from {ordered_answers}) !(tuple:from {ordered_answers})",
            [["[a b b]"], ["(a b b)"]], profile)
        run("(= (increment $local) (+ $local 1)) "
            "!(list:from (serialize canonical (reify (increment 2))))", [["[3]"]], profile)
        run(f"!(reflect (list:from {ordered_answers}))",
            [["a", "b", "b"]], profile, ordered="[a, b, b]")
        run("!(size-atom (reify (superpose (a b)))) !(car-atom (hold (a b))) "
            "!(index-atom (hold (x y)) 1)", [["2"], ["a"], ["y"]], profile)
        run("!(car-atom (hold (tag $x))) !(size-atom (hold (tag $x)))",
            [["tag"], ["2"]], profile)
        run("!(car-atom (reify (superpose (b a))))",
            [["(Error (car-atom (reify (superpose (b a)))) ExpectedAnswerSequence)"]], profile)
        run(f"!(index-atom {ordered_answers} 3)",
            [[f"(Error (index-atom {ordered_answers} 3) AnswerIndexOutOfBounds)"]], profile)
        constrained = ("(serialize canonical (reify (amb "
                       "(chain (unify $r red) $_ 7) (chain (unify $r blue) $_ 8))))")
        run(f"!(let $s {constrained} (chain (index-atom $s 1) $_ $r))",
            [["blue"]], profile)
        run(f"!(let $s {constrained} (count (reify (chain (unify $r red) $_ "
            "(index-atom $s 1)))))", [["0"]], profile)
        run(f"!(list:from {constrained})",
            [[f"(Error (list:from {constrained}) ConstrainedAnswerProjection)"]], profile)
        run("!(serialize bogus (reify (empty)))",
            [["(Error (serialize bogus (reify (empty))) UnknownSerializeMode)"]], profile)
        run("!(serialize (reify (empty)))",
            [["(Error (serialize (reify (empty))) ExplicitOrderRequired)"]], profile)
        run("!(serialize canonical (reify (delay 7)))",
            [["(Error (serialize canonical (reify (delay 7))) CanonicalAnswerOrderUndefined)"]], profile)
        run("!(count (serialize any (reify (delay 7))))", [["1"]], profile)
        run("!(fold-space + 0 (reify (superpose (5 2 5))))", [["12"]], profile)
        run("!(fold-space + 0 (reify (superpose (2 5 5))))", [["12"]], profile)
        run("!(fold-space + 0 (reify (superpose (1.5 2))))",
            [["(Error 1.5 FoldValueRefused)"]], profile)
        run("(= (duplicate) a) (= (duplicate) a) !(count (collapse (duplicate)))", [["2"]], profile)
        run("!(count (collapse (let $s (collapse (chain (unify $x 0) $_ 7)) "
            "(chain (unify $x 1) $_ (reflect $s)))))", [["0"]], profile)
        run("!(count (collapse (amb (chain (unify $x 0) $_ A) (chain (unify $x 1) $_ B))))", [["2"]], profile)
        run("!(count (collapse (superpose (a b c))))", [], profile,
            extra=("--fuel", "3"), incomplete="fuel-exhausted")
        run("!(count (collapse (if $x A B)))", [], profile, incomplete="incomplete")
        run(two + "!(let $t (delay (two)) (let $a (force $t) "
            "(let $b (force $t) (Pair $a $b))))", [["(Pair 0 0)", "(Pair 1 1)"]], profile)
        run(two + "!(delay $t (two) (let $a (force $t) "
            "(let $b (force $t) (Pair $a $b))))", [["(Pair 0 0)", "(Pair 1 1)"]], profile)
        run(two + "!(let $s (collapse (delay (two))) (let $t (reflect $s) "
            "(let $a (force $t) (let $b (force $t) (Pair $a $b)))))",
            [["(Pair 0 0)", "(Pair 1 1)"]], profile)
        run("!(let $c (hold (new $s $s)) (let $a (run $c) "
            "(let $b (run $c) (count (collapse (unify $a $b))))))", [["0"]], profile)
        run(two + "!(let $c (hold (let $x (two) $x)) "
            "(chain (unify $c (let $renamed (two) $renamed)) $_ "
            "(let $a (run $c) (let $b (run $c) (Pair $a $b)))))", [pairs], profile)
        run("!(chain (unify (lam (ctx $F (idx 0))) (lam (Pair (idx 0) 1))) "
            "$_ (lam (ctx $F (idx 0))))", [["(lam (Pair (idx 0) 1))"]], profile)
        run("!(chain (unify (lam (lam (ctx $F (idx 0) (idx 1)))) "
            "(lam (lam (Pair (idx 1) (idx 0))))) $_ (lam (lam (ctx $F (idx 1) (idx 0)))))",
            [["(lam (lam (Pair (idx 0) (idx 1))))"]], profile)
        run("!(count (collapse (unify (lam (lam (ctx $F (idx 0)))) "
            "(lam (lam (idx 1))))))", [["0"]], profile)
        run("!(chain (unify (lam (lam (ctx $F (idx 1)))) (lam (lam (ctx $G (idx 0))))) $_ "
            "(chain (unify (lam (ctx $F (idx 0))) (lam 7)) $_ (lam (ctx $G (idx 0)))))",
            [["(lam 7)"]], profile)
        run("!(chain (unify (lam (lam (ctx $F (idx 1) (idx 0)))) "
            "(lam (lam (ctx $F (idx 0) (idx 1))))) $_ "
            "(chain (unify (lam (lam (ctx $F (idx 1) (idx 0)))) (lam (lam 9))) "
            "$_ (lam (lam (ctx $F (idx 0) (idx 1))))))", [["(lam (lam 9))"]], profile)
        run("!(chain (unify (lam (lam (ctx $F (idx 1)))) "
            "(lam (lam (Box (ctx $G (idx 1) (idx 0)))))) $_ "
            "(chain (unify (lam (lam (ctx $G (idx 1) (idx 0)))) (lam (lam 8))) "
            "$_ (lam (ctx $F (idx 0)))))", [["(lam (Box 8))"]], profile)
        run("!(count (collapse (unify (lam (ctx $F (idx 0) (idx 0))) (lam 7))))",
            [], profile, incomplete="suspended")
        run("!(count (collapse (unify (lam (ctx $F (Pair (idx 0) 0))) (lam 7))))",
            [], profile, incomplete="suspended")
        pattern = "(Pair (lam (lam (ctx $F (idx 0) (idx 1)))) (lam (ctx $F (idx 0) (idx 0))))"
        run(f"!(chain (unify {pattern} (Pair (lam (lam 7)) (lam 7))) $_ matched)",
            [["matched"]], profile)
        run(f"!(count (reify (unify {pattern} (Pair (lam (lam 7)) (lam 8)))))",
            [["0"]], profile)
        run("!(bind! &s (new-space)) !(add-atom &s (Pair (lam (lam 7)) (lam 7))) "
            f"!(count (transform &s (take {pattern}) done))",
            [["()"], ["()"], ["1"]], profile)
        run("(= (needs (lam (ctx $F (idx 0) (idx 0)))) yes) !(needs (lam 7))",
            [], profile, incomplete="suspended")
        run("(= (needs (lam (ctx $F (idx 0) (idx 0)))) yes) !(needs different)",
            [["(needs different)"]], profile)
        run("(= (needs (lam 7)) ready) (= (other 0) irrelevant) "
            "(= (needs (lam (ctx $F (idx 0) (idx 0)))) pending) "
            "!(count (reify (needs (lam 7))))", [], profile, incomplete="suspended")
        run("(= (pick 1) first) (= ($head 1) fallback) (= (pick 1) first) "
            "(= (another 1) irrelevant) !(pick 1)",
            [["first", "fallback", "first"]], profile, ordered="[first, fallback, first]")
        run("(= (ABTBind lam (Binders x) x) binder-matched) !(lam (idx 0))",
            [["binder-matched"]], profile)
        run("(= (same-scalar $x $x) equal) "
            "!(same-scalar 7 7) !(same-scalar 7 8) !(same-scalar 7 7.0)",
            [["equal"], ["(same-scalar 7 8)"], ["(same-scalar 7 7.0)"]], profile)
        run("(: All (AbtSignature All (Fields domain body) (Bind1 body))) "
            "!(chain (unify (All Ty (ctx $F (idx 0))) (All Ty (Pair (idx 0) 1))) "
            "$_ (All Ty (ctx $F (idx 0)))) "
            "!(count (reify (unify (All Ty $F) (All Ty (idx 0))))) "
            "!(count (reify (unify (All (idx 0) 7) (All (idx 0) 7))))",
            [["(All Ty (Pair (idx 0) 1))"], ["0"], ["0"]], profile)
        run(f'!(import! &self "{fixtures / "library.metta"}") !(imported-successor 8) '
            '!(imported-hold (+ 7 8))',
            [["()"], ["9"], ["(+ 7 8)"]], profile)
        run("(= (a $x) activated) !(superpose (a b))", [["a", "b"]], profile)
        run("(schematic $local) "
            "!(chain (match &self (schematic red) seen) $_ (match &self (schematic blue) seen))",
            [["seen"]], profile)
        run("!(add-atom &self (captured $x)) "
            "!(count (reify (chain (match &self (captured red) seen) "
            "$_ (match &self (captured blue) leak))))",
            [["()"], ["0"]], profile)
        run("!(chain (add-atom &self (item $a)) $_ "
            "(chain (add-atom &self (item $b)) $_ (add-atom &self (first-name $a)))) "
            "!(chain (match &self (first-name red) go) $_ "
            "(chain (transform &self (take (item blue)) spent) $_ "
            "(count (reify (match &self (item red) ok)))))",
            [["()"], ["1"]], profile)
        run(two + "!(bind! &s (new-space)) !(add-atom &s (tag 5)) "
            "!(let $c (match &s (tag $x) (let $x (two) $x)) "
            "(let $a (run $c) (let $b (run $c) (Pair $a $b))))",
            [["()"], ["()"], pairs], profile)
        scoped = "!(bind! &s (new-space)) !(add-atom &s (lam (Pair (idx 0) 1))) "
        run(scoped + "!(reflect (transform &s (take (lam (ctx $F (idx 0)))) "
            "(lam (ctx $F (idx 0)))))", [["()"], ["()"], ["(lam (Pair (idx 0) 1))"]], profile)
        run(scoped + "!(count (transform &s (take (lam $F)) no)) "
            "!(count (collapse (match &s $x $x)))", [["()"], ["()"], ["0"], ["1"]], profile)
        run(scoped + "!(count (transform &s (take (lam (ctx $F (idx 0) (idx 0)))) no))",
            [], profile, incomplete="suspended")
        run("!(bind! &s (new-space)) !(add-atom &s (ABTBind lam (Binders x) x)) "
            "!(count (transform &s (take (ABTBind lam (Binders y) y)) done))",
            [["()"], ["()"], ["1"]], profile)
    setup = "!(bind! &s (new-space)) !(add-atom &s (token 1)) "
    run(setup + "!(chain (transform &s (take (token $x)) (made $x)) $_ $x)",
        [["()"], ["()"], ["1"]])
    run(setup + "!(count (transform &s (, (take (token $x)) (let True (unify $z 7))) (made $x $z))) "
        "!(match &s (made $x $z) (Pair $x $z))",
        [["()"], ["()"], ["1"], ["(Pair 1 7)"]])
    run("!(bind! &s (new-space)) !(bind! &alias &s) !(add-atom &s only) "
        "!(count (transform &s (, (take only) (take &alias only)) both)) "
        "!(count (reify (match &s only spare)))",
        [["()"], ["()"], ["()"], ["0"], ["1"]])
    ground_firing = ("!(bind! &s (new-space)) !(add-atom &s coin) !(add-atom &s witness) "
                     "!(reflect (transform &s (, (take coin) (read witness)) paid)) "
                     "!(count (reify (match &s $row $row)))")
    ground_answers = [["()"], ["()"], ["()"], ["paid"], ["2"]]
    protocol = {"CETTA_TRANSFORM_ENGINE": "tcalc", "CETTA_TRANSFORM_TRACE": "1",
                "CETTA_TRANSFORM_TCALC_CHECKED": "1", "CETTA_TRANSFORM_TCALC_CARRIER": "host"}
    run(ground_firing, ground_answers,
        environment=dict(protocol, CETTA_TRANSFORM_ENGINE="direct"), trace="(engine direct)")
    run(ground_firing, ground_answers, environment=protocol, trace="(engine tcalc)")
    run(ground_firing, ground_answers,
        environment=dict(protocol, CETTA_TRANSFORM_TCALC_CARRIER="terms"), trace="(engine tcalc)")
    run(ground_firing, ground_answers, extra=("--fuel", "1000"),
        environment=protocol, trace="(engine direct) (requested-engine tcalc)")
    run("!(bind! &s (new-space)) !(add-atom &s (ABTBind lam (Binders x) x)) "
        "!(count (transform &s (take (ABTBind lam (Binders y) y)) done))",
        [["()"], ["()"], ["1"]], environment=protocol,
        trace="(engine direct) (requested-engine tcalc)")
    for profile in ("native", "eager"):
        fixture = fixtures / "transactions.metta"
        result = subprocess.run([binary, "--lang", "tmetta", "--profile", profile,
            str(fixture)], text=True, capture_output=True, timeout=90)
        assert result.returncode == 0, result.stderr
        expected = [bag(line) for line in fixture.with_suffix(".expected").read_text().splitlines()]
        assert [bag(line) for line in result.stdout.splitlines() if line.strip()] == expected, result
        count += len(expected)
    print(f"PASS: {count} tmetta end-to-end checks")


if __name__ == "__main__":
    main()
