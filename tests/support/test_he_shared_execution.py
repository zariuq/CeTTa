#!/usr/bin/env python3
"""Ordered HE observations at compiled and raw continuation boundaries."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


# The last field requires the shared machine. Intrinsic demands and closed
# calls can select their existing qualified realizations.
CASES = [
    ("shared-dag-type-input", "(= (dag $n $leaf) (if (== $n 0) $leaf "
     "(let $t (dag (- $n 1) $leaf) (node $t $t))))\n"
     "!(== (dag 40 1) (dag 40 1.0))\n"
     "!(== (dag 40 1) (dag 40 2))\n", "[True]\n[False]\n", False),
    ("duplicates", "(= (choose) 1)\n(= (choose) 1)\n(= (choose) 2)\n"
     "!(let $v (choose) ($v $v))\n", "[(1 1), (1 1), (2 2)]\n", False),
    ("ordered-bindings", "(= (row 1) a)\n(= (row 1) a)\n(= (row 2) b)\n"
     "!(let $v (row $k) ($k $v $k))\n", "[(1 a 1), (1 a 1), (2 b 2)]\n", True),
    ("caller-alias", "(= (same $x $x) ok)\n"
     "!(let $v (same $a 2) ($a $v))\n", "[(2 ok)]\n", True),
    ("caller-variable", "(= (fresh $x) (box $x $hole))\n!(fresh $caller)\n",
     "[(box $caller $hole#0)]\n", True),
    ("bare-caller-variable", "(= (identity $x) $x)\n!(identity $caller)\n",
     "[$caller]\n", True),
    ("fresh-scalar-result", "(= (name) $named)\n!(name)\n"
     "!(let $v (name) ($v $v))\n"
     "(= (names) $alpha)\n(= (names) $beta)\n"
     "!(let $v (names) ($v $v))\n"
     "(= (pick priority) $fast)\n"
     "(= (route $x $payload) (let $dst (pick $x) (send $dst $payload)))\n"
     "!(route priority $payload)\n",
     "[$named#0]\n[($named#1 $named#1)]\n"
     "[($alpha#2 $alpha#2), ($beta#3 $beta#3)]\n"
     "[(send $fast#4 $payload)]\n", True),
    ("correlated-primitive-types", "!(== 5 \"S\")\n",
     '[(Error (== 5 "S") (BadArgType 2 Number String))]\n', False),
    ("correlated-tuple-types", "!(== (42 (1 2 3)) (42 1 2 3))\n",
     '[(Error (== (42 (1 2 3)) (42 1 2 3)) '
     '(BadArgType 2 (Number (Number Number Number)) (Number Number Number Number)))]\n',
     False),
    ("constructor-fault-before-divergence", "(= (loop) (loop))\n"
     "!(Box (+ 1 \"S\") (loop))\n",
     '[(Error (+ 1 "S") (BadArgType 2 Number String))]\n', True),
    ("constructor-fault-alternatives", "(= (choices) (+ 1 \"S\"))\n"
     "(= (choices) 1)\n(= (choices) 1)\n!(Box (choices) 2)\n",
     '[(Box 1 2), (Box 1 2)]\n',
     True),
    ("fault-suppression-before-empty-consumer", "(= (choices) (+ 1 \"S\"))\n"
     "(= (choices) 1)\n!(Box (choices) (empty))\n", "", True),
    ("backend-form-is-ordinary-he", "(= (first $x) (pair first $x))\n"
     "!(first a)\n", "[(pair first a)]\n", False),
    ("mutation-invalidates-selected-head", "(= (g $x) (+ $x 1))\n"
     "(= (f $g) ($g 1))\n(= (f $g) 42)\n!(collapse (f g))\n"
     "!(remove-atom &self (= (f $g) 42))\n!(collapse (f g))\n",
     "[(2 42)]\n[()]\n[(2)]\n", True),
    ("occurs-check", "(= (same $x $x) yes)\n!(same $y (node $y))\n",
     "[(same $y (node $y))]\n", True),
    ("entered-empty", "(= (zero) (empty))\n!(zero)\n"
     "!(let $v (zero) unexpected)\n", "", False),
    ("arity", "(= (f $x) 1)\n!(f)\n!(f 1 2)\n",
     "[(f)]\n[(f 1 2)]\n", False),
    ("literal-pattern", "(= (pattern $x) (box $x))\n"
     "!(let (pattern $v) (box 3) $v)\n", "", False),
    ("callback-effect", "(= (apply $f $x) ($f $x))\n"
     "!(apply println! 3)\n", "3\n[()]\n", False),
    ("pattern-callback-effect", "(= (pick println!) 7)\n"
     "(= (run) (let $x (pick $f) ($f $x)))\n!(run)\n", "7\n[()]\n", False),
    ("prime-context-heads-are-ordinary-he",
     "(= (ctx:capture $x) (captured-in-he $x))\n"
     "(= (context:self) context-in-he)\n"
     "!(ctx:capture token)\n!(context:self)\n",
     "[(captured-in-he token)]\n[context-in-he]\n", False),
    ("source-demand", "(: held (-> Atom Atom))\n"
     "(= (held $x) $x)\n!(held (println! unexpected))\n",
     "[(println! unexpected)]\n", False),
    ("minimal-return", "!(function (chain (eval (+ 1 2)) $x (return $x)))\n",
     "[3]\n", False),
    ("minimal-no-return", "!(function (chain (eval (+ 1 2)) $x $x))\n",
     "[(Error (function (chain (eval (+ 1 2)) $x $x)) NoReturn)]\n", False),
    ("match-caller-alias", "(row 1 a)\n(row 1 a)\n(row 2 b)\n"
     "(= (select $x) (match &self (row $x $v) (let $w $v ($x $w))))\n"
     "!(let $out (select $caller) ($caller $out))\n",
     "[(1 (1 a)), (1 (1 a)), (2 (2 b))]\n", False),
    ("native-source-fault", "!(import! &self mork)\n"
     "!(bind! &bad (Error bad payload))\n"
     "!(mork:add-atom &bad (edge a b))\n",
     '[()]\n[()]\n[(Error (mork:add-atom (Error bad payload) (edge a b)) '
     'expected MorkSpace as first argument)]\n', False),
    ("nop-effects", "(= (producer) (let $u (println! first) 1))\n"
     "(= (producer) (let $u (println! second) 2))\n!(nop (producer))\n",
     "first\nsecond\n[()]\n", False),
    ("bind-completed-producer", "(= (producer) (let $u (println! first) 1))\n"
     "(= (producer) (let $u (println! second) 2))\n"
     "!(bind! &kept (producer))\n! &kept\n", "first\nsecond\n[()]\n[1]\n", False),
    ("collapse-bind-aliases", "(= (row 1) a)\n(= (row 1) a)\n(= (row 2) b)\n"
     "!(let $pairs (collapse-bind (row $k)) "
     "(let $v (superpose-bind $pairs) ($k $v)))\n",
     "[(1 a), (1 a), (2 b)]\n", False),
    ("state-values", "!(let $n (/ 0.0 0.0) "
     "(let $s (new-state $n) (== $s $s)))\n"
     "!(let $n (/ 0.0 0.0) (let $s (new-state $n) (get-state $s)))\n"
     "!(let $n (/ 0.0 0.0) "
     "(let $s (new-state $n) (== (foo $s) (foo $s))))\n",
     "[False]\n[NaN]\n[False]\n", False),
    ("state-alternatives", "!(bind! &cell (new-state 0))\n"
     "(= (refs) &cell)\n(= (refs) &cell)\n"
     "(= (values) 1)\n(= (values) 2)\n(= (values) 2)\n"
     "!(change-state! (refs) (values))\n!(get-state &cell)\n",
     "[()]\n[(State 2), (State 2), (State 2), (State 2), (State 2), (State 2)]\n[2]\n",
     False),
    ("state-bindings", "(= (refs a) (new-state 7))\n"
     "(= (refs b) (new-state 8))\n"
     "!(let $v (get-state (refs $which)) ($which $v))\n"
     "!(let $s (change-state! (refs $which) 9) ($which (get-state $s)))\n",
     "[(a 7), (b 8)]\n[(a 9), (b 9)]\n", False),
    ("state-effects", "!(bind! &cell (new-state 0))\n"
     "(= (values) (let $x (println! one) 1))\n"
     "(= (values) (let $x (println! two) 2))\n"
     "!(change-state! &cell (values))\n!(get-state &cell)\n",
     "one\ntwo\n[()]\n[(State 2), (State 2)]\n[2]\n", False),
]

PROFILE_CASES = [
    ("dependent-binder-demand", "(: Fact Type)\n(: fact Fact)\n"
     "(: WitnessOf (-> $p Type))\n"
     "(: remember (-> (: $p Fact) (WitnessOf $p)))\n"
     "!(remember fact)\n!(get-type (remember fact))\n",
     "[(remember fact)]\n[(WitnessOf fact)]\n"),
    ("dependent-result-normalization", "(: Truth Type)\n"
     "(: truth (-> Number Truth))\n(: stamp (-> Bool Truth))\n"
     "(= (stamp False) (truth 0))\n(type-level-function stamp)\n"
     "(: Tagged (-> Truth Type))\n(: Premise (-> Bool Type))\n"
     "(: evidence (Premise False))\n"
     "(: maker (-> (Premise $q) (Tagged (stamp $q))))\n"
     "(: use (-> (Tagged (truth 0)) Number))\n(= (use $x) 7)\n"
     "!(use (maker evidence))\n", "[7]\n"),
]

DEEP_CASES = [
    ("ordinary-tail", "(= (down $n) (if (== $n 0) done "
     "(down (- $n 1))))\n!(down 20000)\n", "[done]\n"),
    ("ordinary-non-tail", "(= (down $n) (if (== $n 0) 0 "
     "(+ 1 (down (- $n 1)))))\n!(down 20000)\n", "[20000]\n"),
    ("selected-head", "(= (down $n $f) (if (== $n 0) done "
     "($f (- $n 1) $f)))\n!(down 20000 down)\n", "[done]\n"),
    ("nop-continuation", "(= (down $n) (if (== $n 0) 0 "
     "(let $u (nop (down (- $n 1))) 0)))\n!(down 20000)\n", "[0]\n"),
    ("collapse-bind-continuation", "(= (down $n) (if (== $n 0) done "
     "(let $pairs (collapse-bind (down (- $n 1))) done)))\n!(down 20000)\n", "[done]\n"),
    ("bind-continuation", "(= (down $n) (if (== $n 0) 0 "
     "(let $u (bind! &kept (down (- $n 1))) 0)))\n!(down 20000)\n", "[0]\n"),
    ("typed-arguments", "(: descend (-> Number Number))\n"
     "(= (descend $n) (if (== $n 0) 0 (+ 1 (descend (- $n 1)))))\n"
     "!(descend 20000)\n", "[20000]\n"),
    ("condition-producer", "(: descend (-> Number Bool))\n"
     "(= (descend $n) (if (== $n 0) True "
     "(if (descend (- $n 1)) True False)))\n"
     "!(descend 20000)\n", "[True]\n"),
    ("let-producer", "(: descend (-> Number Number))\n"
     "(= (descend $n) (if (== $n 0) 0 "
     "(let $v (descend (- $n 1)) (+ $v 1))))\n"
     "!(descend 20000)\n", "[20000]\n"),
    ("raw-full-crossing", "(: descend (-> Number %Undefined%))\n"
     "(= (descend $n) (if (== $n 0) done "
     "(function (chain (metta (descend (- $n 1)) %Undefined% &self) "
     "$v (return done)))))\n"
     "!(descend 20000)\n", "[done]\n"),
    ("match-continuation", "(ticket ok)\n(: descend (-> Number Number))\n"
     "(= (descend $n) (if (== $n 0) 0 "
     "(match &self (ticket ok) "
     "(let $v (descend (- $n 1)) (+ $v 1)))))\n"
     "!(descend 20000)\n", "[20000]\n"),
    ("case-continuation", "(: descend (-> Number Bool))\n"
     "(= (descend $n) (if (== $n 0) True "
     "(case (descend (- $n 1)) ((True True) (False False)))))\n"
     "!(descend 20000)\n", "[True]\n"),
    ("collapse-continuation", "(: descend (-> Number Number))\n"
     "(= (descend $n) (if (== $n 0) 1 "
     "(let $bag (collapse (descend (- $n 1))) (size-atom $bag))))\n"
     "!(descend 20000)\n", "[1]\n"),
    ("captured-continuation", "(: descend (-> Number Number))\n"
     "(= (descend $n) (if (== $n 0) 0 "
     "(let $f capture (let $v ($f (descend (- $n 1))) (+ $v 1)))))\n"
     "!(descend 20000)\n", "[20000]\n"),
    ("space-producer", "(ticket ok)\n"
     "(= (origin $n) (if (== $n 0) &self (origin (- $n 1))))\n"
     "!(match (| &self (origin 20000)) (ticket $x) $x)\n", "[ok, ok]\n"),
    ("switch-continuation", "(= (descend $n) "
     "(switch $n ((0 done) ($_ (descend (- $n 1))))))\n"
     "!(descend 20000)\n", "[done]\n"),
    ("state-read-producer", "!(bind! &cell (new-state 7))\n"
     "(= (descend $n) (if (== $n 0) &cell "
     "(let $v (get-state (descend (- $n 1))) &cell)))\n"
     "!(get-state (descend 20000))\n", "[()]\n[7]\n"),
    ("state-reference-producer", "!(bind! &cell (new-state 7))\n"
     "(= (descend $n) (if (== $n 0) &cell "
     "(change-state! (descend (- $n 1)) 7)))\n"
     "!(get-state (descend 20000))\n", "[()]\n[7]\n"),
    ("state-value-producer", "!(bind! &cell (new-state 7))\n"
     "(= (descend $n) (if (== $n 0) 7 "
     "(get-state (change-state! &cell (descend (- $n 1))))))\n"
     "!(descend 20000)\n", "[()]\n[7]\n"),
]

# Finite fuel retains the dialect's boundary behavior, including publication
# after a source that was held when its evaluation budget reached zero.
FUEL_CASES = [
    ("duplicates", 1, ""),
    ("duplicates", 3, "[(1 1), (1 1), (2 2)]\n"),
    ("minimal-return", 3, ""),
    ("minimal-return", 7, "[3]\n"),
    ("entered-empty", 1, "[unexpected]\n"),
    ("entered-empty", 3, ""),
    ("source-demand", 3, "[(println! unexpected)]\n"),
    ("state-effects", 3, "[()]\n[0]\n"),
]


def private_names(text):
    """Compare fresh private frames by a bijection; caller names stay literal."""
    names = {}

    def rename(match):
        suffix = match.group(0)
        return names.setdefault(suffix, "#" + str(len(names)))

    return re.sub(r"#[0-9]+", rename, text)


def main():
    binary = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parents[2]
    observations = 0
    with tempfile.TemporaryDirectory(prefix="he-shared-", dir=root / "runtime") as directory:
        for name, source, expected, compiled in CASES:
            path = Path(directory) / (name + ".metta")
            path.write_text(source)
            for canonical, enabled in (("0", "0"), ("0", "1"),
                                       ("1", "0"), ("1", "1"), (None, None)):
                env = dict(os.environ, CETTA_PETTA_MACHINE_STATS="1")
                for key, value in (("CETTA_HE_SHARED_EXECUTION", enabled),
                                   ("CETTA_HE_CANONICAL_EXECUTION", canonical)):
                    if value is None:
                        env.pop(key, None)
                    else:
                        env[key] = value
                result = subprocess.run([str(binary), "--lang", "he", str(path)],
                                        cwd=root, env=env, capture_output=True,
                                        text=True, timeout=30)
                if result.returncode or private_names(result.stdout) != expected:
                    raise AssertionError((name, canonical, enabled, result.returncode,
                                          result.stdout, result.stderr))
                if enabled != "0" and compiled and "PETTA_MACHINE_STATS" not in result.stderr:
                    raise AssertionError((name, "compiled route was not exercised"))
                if name == "pattern-callback-effect" and any(
                    int(value) != 0 for value in re.findall(
                        r"(?:goal_host|equation_match_attempts)=([0-9]+)", result.stderr)):
                    raise AssertionError((name, "pattern callback bypassed admission"))
                observations += 1
        for name, source, expected in PROFILE_CASES:
            path = Path(directory) / (name + ".metta")
            path.write_text(source)
            for canonical, enabled in (("0", "0"), ("0", "1"),
                                       ("1", "0"), ("1", "1"), (None, None)):
                env = dict(os.environ)
                for key, value in (("CETTA_HE_CANONICAL_EXECUTION", canonical),
                                   ("CETTA_HE_SHARED_EXECUTION", enabled)):
                    if value is None:
                        env.pop(key, None)
                    else:
                        env[key] = value
                result = subprocess.run([str(binary), "--lang", "he",
                                         "--profile", "he-prime", str(path)],
                                        cwd=root, env=env, capture_output=True,
                                        text=True, timeout=30)
                if result.returncode or result.stdout != expected or result.stderr:
                    raise AssertionError((name, canonical, enabled, result.returncode,
                                          result.stdout, result.stderr))
                observations += 1
        for name, fuel, expected in FUEL_CASES:
            path = Path(directory) / (name + ".metta")
            for enabled in ("0", "1"):
                env = dict(os.environ, CETTA_HE_CANONICAL_EXECUTION="1",
                           CETTA_HE_SHARED_EXECUTION=enabled,
                           CETTA_OPEN_EQUATIONS_REFERENCE="1",
                           CETTA_PREPARED_PURE_REFERENCE="1")
                result = subprocess.run([str(binary), "--lang", "he", "--fuel",
                                         str(fuel), str(path)], cwd=root, env=env,
                                        capture_output=True, text=True, timeout=30)
                if result.returncode or result.stdout != expected or result.stderr:
                    raise AssertionError((name, fuel, enabled, result.returncode,
                                          result.stdout[:4096], result.stderr[:4096]))
                observations += 1
        # A raw equation returns another instruction. The delimiter continues
        # it; a raw invocation alone retains instruction syntax as a value.
        source = """(= (descend $n)
  (chain (eval (== $n 0)) $zero
    (unify $zero True (return done)
      (chain (eval (- $n 1)) $next (eval (descend $next))))))
!(function (eval (descend 20000)))
"""
        path = Path(directory) / "deep-instructions.metta"
        path.write_text(source)
        env = dict(os.environ, CETTA_HE_SHARED_EXECUTION="0",
                   CETTA_OPEN_EQUATIONS_REFERENCE="1",
                   CETTA_PREPARED_PURE_REFERENCE="1")
        result = subprocess.run([str(binary), "--lang", "he", str(path)],
                                cwd=root, env=env, capture_output=True,
                                text=True, timeout=30)
        if result.returncode or result.stdout != "[done]\n" or result.stderr:
            raise AssertionError(("deep instructions", result.returncode,
                                  result.stdout, result.stderr))
        observations += 1
        # Plain values stay on the dialect's value/cast boundary; interpreting
        # them must not allocate a relational computation episode.
        path = Path(directory) / "plain-values.metta"
        path.write_text('! 16\n! spelling\n! $caller\n! True\n'
                        '(= (plain $n) (if (< $n 10) 16 17))\n!(plain 3)\n')
        env = dict(os.environ, CETTA_HE_CANONICAL_EXECUTION="1",
                   CETTA_HE_SHARED_EXECUTION="1", CETTA_PETTA_MACHINE_STATS="1",
                   CETTA_PREPARED_PURE_REFERENCE="1", CETTA_OPEN_EQUATIONS_REFERENCE="1")
        result = subprocess.run([str(binary), "--lang", "he", str(path)],
                                cwd=root, env=env, capture_output=True,
                                text=True, timeout=30)
        if result.returncode or result.stdout != "[16]\n[spelling]\n[$caller]\n[True]\n[16]\n" or result.stderr:
            raise AssertionError(("plain-values", result.returncode,
                                  result.stdout, result.stderr))
        observations += 1
        # Force a declined closed realization through the general route: its
        # ordered occurrences and correlated uses must still agree.
        name, source, expected, _ = next(case for case in CASES if case[0] == "duplicates")
        path = Path(directory) / "declined-closed-duplicates.metta"
        path.write_text(source)
        env = dict(os.environ, CETTA_HE_CANONICAL_EXECUTION="1",
                   CETTA_HE_SHARED_EXECUTION="1", CETTA_PREPARED_PURE_REFERENCE="1",
                   CETTA_PETTA_MACHINE_STATS="1")
        result = subprocess.run([str(binary), "--lang", "he", str(path)],
                                cwd=root, env=env, capture_output=True,
                                text=True, timeout=30)
        if result.returncode or result.stdout != expected or "PETTA_MACHINE_STATS" not in result.stderr:
            raise AssertionError(("declined-closed-duplicates", result.returncode,
                                  result.stdout, result.stderr))
        observations += 1
        for name, source, expected in DEEP_CASES:
            path = Path(directory) / (name + ".metta")
            path.write_text(source)
            env = dict(os.environ, CETTA_HE_CANONICAL_EXECUTION="1",
                       CETTA_HE_SHARED_EXECUTION="0",
                       CETTA_GC_BUDGET_MB="1",
                       CETTA_OPEN_EQUATIONS_REFERENCE="1",
                       CETTA_PREPARED_PURE_REFERENCE="1")
            result = subprocess.run([str(binary), "--lang", "he", str(path)],
                                    cwd=root, env=env, capture_output=True,
                                    text=True, timeout=30)
            if result.returncode or result.stdout != expected or result.stderr:
                raise AssertionError((name, result.returncode,
                                      result.stdout[:4096], result.stderr[:4096]))
            observations += 1
        # The first answer remains in its completed-producer bank while a
        # later alternative allocates enough to require frontier collection.
        # Both copies of each completed answer must keep the same bindings.
        source = """(= (burn $n) (if (== $n 0) 7 (burn (- $n 1))))
(= (rows $k) (box $k $k))
(= (rows $k) (let $u (burn 18000) (box $u $k)))
(= (rows $k) (let $u (burn 18000) (box $u $k)))
!(let $v (rows 7) ($v $v))
"""
        path = Path(directory) / "completion-bank-relocation.metta"
        path.write_text(source)
        expected = "[((box 7 7) (box 7 7)), ((box 7 7) (box 7 7)), " \
                   "((box 7 7) (box 7 7))]\n"
        for enabled in ("0", "1"):
            env = dict(os.environ, CETTA_HE_CANONICAL_EXECUTION="1",
                       CETTA_HE_SHARED_EXECUTION=enabled,
                       CETTA_GC_BUDGET_MB="1", CETTA_PETTA_MACHINE_STATS="1",
                       CETTA_OPEN_EQUATIONS_REFERENCE="1",
                       CETTA_PREPARED_PURE_REFERENCE="1")
            result = subprocess.run([str(binary), "--lang", "he", str(path)],
                                    cwd=root, env=env, capture_output=True,
                                    text=True, timeout=30)
            if result.returncode or result.stdout != expected:
                raise AssertionError(("completion bank relocation", enabled,
                                      result.returncode, result.stdout, result.stderr))
            if enabled == "1" and not any(int(count) > 0 for count in
                    re.findall(r"choice_nursery_evacuations=([0-9]+)", result.stderr)):
                raise AssertionError(("completion bank was not collected", result.stderr))
            observations += 1
    print(f"PASS: {observations} ordered HE compiled and raw-continuation observations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
