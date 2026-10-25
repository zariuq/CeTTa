#!/usr/bin/env python3
"""Owned consumer observations: demand, duplicate order, effects and faults."""
from pathlib import Path
import argparse
import itertools
import os
import subprocess
import sys
import tempfile

# Expected observations use the committed dialect runtime and eligible frozen
# SWI-PeTTa v1.0.6 controls. Invocation cases retain PeTTa's declared dispatch,
# live-declaration and held-argument contracts where the reference differs.
# Cases include refusals of fusion: they must retain ordinary evaluation.
CASES = [
    ('scope-caller-alternatives', 'petta', '(= (inner) (let $_ (cut) 1))\n(= (inner) 2)\n(= (outer) (let $before (superpose (a b)) (Pair $before (inner))))\n!(outer)\n', '(Pair a 1)\n(Pair b 1)\n', 0),
    ('scope-earlier-subgoal-and-later-duplicates', 'petta', '(= (inner) (superpose (1 2)))\n(= (outer) (let $x (inner) (let $_ (cut) (let $later (superpose (7 7)) (Pair $x $later)))))\n(= (outer) discarded)\n!(outer)\n', '(Pair 1 7)\n(Pair 1 7)\n', 0),
    ('scope-handler-transparency', 'petta', '(= (outer) (catch (let $_ (cut) (/ 1 0))))\n(= (outer) 9)\n!(let $answers (collapse (outer)) (size-atom $answers))\n', '1\n', 0),
    ('scope-committed-failure', 'petta', '(= (outer) (let $_ (println! entered) (let $_ (cut) (empty))))\n(= (outer) (println! forbidden))\n!(collapse (outer))\n', 'entered\n()\n', 0),
    ('scope-cut-shaped-data', 'petta', '(= (outer) (let $v (quote (cut)) (let $_ $v 1)))\n(= (outer) 2)\n!(collapse (outer))\n', '(1 2)\n', 0),
    ('scope-once-restores-caller-scope', 'petta', '(= (outer) (let $x (once (superpose (1 2))) (let $_ (cut) $x)))\n(= (outer) 9)\n!(collapse (outer))\n', '(1)\n', 0),
    ('scope-callee-return-restores-scope', 'petta', '(= (inner) (superpose (1 2)))\n(= (outer) (let $x (inner) (let $_ (cut) $x)))\n(= (outer) 9)\n!(collapse (outer))\n', '(1)\n', 0),
    ('scope-handled-error-keeps-new-choices', 'petta', '(= (outer) (let $_ (catch (/ 1 0)) (superpose (7 7))))\n(= (outer) 9)\n!(collapse (outer))\n', '(7 7 9)\n', 0),
    ('invocation-captured-data', 'petta', '(= (transport $a $b $c) (Bundle $a $b $c))\n!(let $p (transport (quote (println! forbidden))) ($p 7 9))\n', '(Bundle (println! forbidden) 7 9)\n', 0),
    ('invocation-argument-effects', 'petta', '(= (transport $a $b) (let $_ (println! body) (Pair $a $b)))\n(= (tick $x) (let $_ (println! $x) $x))\n!(let $p (transport (tick 1)) ($p (tick 2)))\n', '1\n2\nbody\n(Pair 1 2)\n', 0),
    ('invocation-body-handler', 'petta', '(= (transport $a $b) (/ 1 0))\n(= (transport $a $b) (Pair $a $b))\n!(catch (collapse (let $p (transport 1) ($p 2))))\n', '((Pair 1 2))\n', 0),
    ('invocation-argument-handler', 'petta', '(= (transport $a $b) (Pair $a $b))\n!(let $caught (catch (let $p (transport 1) ($p (/ 1 0)))) (car-atom $caught))\n', 'Error\n', 0),
    ('invocation-cut', 'petta', '(= (transport $a $b) (let $_ (cut) (Pair $a $b)))\n(= (transport $a $b) discarded)\n!(collapse (let $p (transport 1) ($p 2)))\n', '((Pair 1 2))\n', 0),
    ('invocation-failure', 'petta', '(= (transport $a $b) (empty))\n!(collapse (let $p (transport 1) ($p 2)))\n', '()\n', 0),
    ('invocation-aliases', 'petta', '(= (transport $a $b) (let $a $b (Pair $a $b)))\n!(let $p (transport $z) (let $v ($p 7) (Pair $v $z)))\n', '(Pair (Pair 7 7) 7)\n', 0),
    ('invocation-held-type', 'petta', '(: transport (-> Atom Atom Atom))\n(= (transport $a $b) (Pair $a $b))\n!(let $p (transport (quote (+ 1 2))) ($p (+ 4 5)))\n', '(Pair (quote (+ 1 2)) (+ 4 5))\n', 0),
    ('invocation-changed-declaration', 'petta', '(= (transport $a $b) (Pair $a $b))\n!(let $p (transport 1) (let $_ (add-atom &self (: transport (-> Number Atom Atom))) ($p (+ 2 3))))\n', '(Pair 1 (+ 2 3))\n', 0),
    ('invocation-changed-arity', 'petta', '(= (transport $a $b) (Pair $a $b))\n!(let $p (transport 1) (let $_ (remove-atom &self (= (transport $a $b) (Pair $a $b))) (let $_ (add-atom &self (= (transport $a $b $c) (Triple $a $b $c))) ($p 2))))\n', '(partial transport (1 2))\n', 0),
    ('result-demand-before-recursion', 'petta', '(= (result-loop $n) (Right (result-loop (+ $n 1))))\n(= (result-consumer $n) (let (Left $x) (result-loop $n) $x))\n!(result-consumer 0)\n', '', 0),
    ('result-demand-prebound', 'petta', '(= (result-loop $n) (Right (result-loop (+ $n 1))))\n(= (result-consumer $n) (let $x (quote (Left 7)) (let $x (result-loop $n) accepted)))\n!(result-consumer 0)\n', '', 0),
    ('result-demand-variable-observation', 'petta', '(= (observe $x) (let (Pair False 7) (Pair (is-var $x) $x) accepted))\n!(observe $x)\n', 'accepted\n', 0),
    ('result-demand-retained-alternatives', 'petta', '(= (result-project $n $v) (if (> $n 0) a $v))\n(= (result-choice $n $ignored) (if (> $n 0) (let a (result-choice 1 $free) (s $n)) a))\n(= (result-choice $n $ignored) (let (k2 a a) (result-project (- $n 1) (k2 a a)) a))\n!(result-choice 2 $any)\n', '(s 2)\n', 0),
    ('result-demand-duplicates', 'petta', '(= (result-loop $n) (Right (result-loop (+ $n 1))))\n(= (result-producer $n) (Right (result-loop $n)))\n(= (result-producer $n) (Left 7))\n(= (result-producer $n) (Left 7))\n(= (result-consumer $n) (let (Left $x) (result-producer $n) $x))\n!(result-consumer 0)\n', '7\n7\n', 0),
    ('result-demand-argument-effect', 'petta', '(= (argument-tick $n) (let $_ (println! argument) $n))\n(= (result-producer $n) (Right (println! forbidden-body)))\n(= (result-consumer) (let (Left $x) (result-producer (argument-tick 0)) $x))\n!(result-consumer)\n', 'argument\n', 0),
    ('nested-choice-activation-owner', 'petta', '(= (choose $n) (if (> $n 0) (empty) (superpose ((let $ignored (collapse $n) nil) (s $n)))))\n(= (choose $n) 3)\n!(choose 0)\n', 'nil\n(s 0)\n3\n', 0),
    ('choice-expression-role', 'petta', '!(collapse (superpose ((+ 1 2) (+ 1 2) (empty) Empty)))\n', '(3 3 Empty)\n', 0),
    ('choice-value-role', 'petta', '!(let $xs (quote ((println! forbidden) Empty (empty))) (collapse (superpose $xs)))\n', '((println! forbidden) Empty (empty))\n', 0),
    ('choice-effect-order', 'petta', '(= (emit $n) (let $_ (println! $n) (if (== $n 0) (empty) $n)))\n!(collapse (superpose ((emit 1) (emit 0) (emit 1))))\n', '1\n0\n1\n(1 1)\n', 0),
    ('choice-first-effect', 'petta', '(= (emit $n) (let $_ (println! $n) $n))\n!(once (superpose ((emit 1) (emit 2))))\n', '1\n1\n', 0),
    ('choice-cut-scope', 'petta', '(= (one) (superpose ((let $_ (cut) 1) (let $_ (println! forbidden) 2))))\n!(one)\n', '1\n', 0),
    ('choice-raw-code', 'he', '!(function (chain (eval (superpose ((+ 1 2) (+ 1 2)))) $v (return $v)))\n', '[(+ 1 2), (+ 1 2)]\n', 0),
    ('choice-raw-zero', 'he', '!(function (chain (eval (superpose ())) $v (return $v)))\n', '', 0),
    ('choice-source-zero', 'he', '!(superpose ((+ 1 2) (+ 1 2) (empty)))\n', '[3, 3]\n', 0),
    ('ordered-duplicates', 'he', '(= (choice) 3)\n(= (choice) 3)\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[3]\n[11]\n', 0),
    ('ordered-duplicates', 'petta', '(= (choice) 3)\n(= (choice) 3)\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '3\n11\n', 0),
    ('empty', 'he', '(= (choice) (empty))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[0]\n[0]\n', 0),
    ('empty', 'petta', '(= (choice) (empty))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '0\n0\n', 0),
    ('late-effect', 'he', '(= (choice) 3)\n(= (choice) (let $ignored (println! late-effect) 5))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', 'late-effect\nlate-effect\n[2]\n[8]\n', 0),
    ('late-effect', 'petta', '(= (choice) 3)\n(= (choice) (let $ignored (println! late-effect) 5))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', 'late-effect\nlate-effect\n2\n8\n', 0),
    ('late-fault', 'he', '(= (choice) 3)\n(= (choice) (/ 1 0))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[1]\n[3]\n', 0),
    ('late-fault', 'petta', '(= (choice) 3)\n(= (choice) (/ 1 0))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '', 2),
    ('unselected-effect', 'he', '(= (choice) (if True 3 (println! unselected-effect)))\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[2]\n[8]\n', 0),
    ('unselected-effect', 'petta', '(= (choice) (if True 3 (println! unselected-effect)))\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '2\n8\n', 0),
    ('conditional', 'he', '(= (choice) (if (< 1 2) (+ 1 2) 5))\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[2]\n[8]\n', 0),
    ('conditional', 'petta', '(= (choice) (if (< 1 2) (+ 1 2) 5))\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '2\n8\n', 0),
    ('non-number', 'he', '(= (choice) 3)\n(= (choice) held)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[2]\n[(+ 3 held)]\n', 0),
    ('non-number', 'petta', '(= (choice) 3)\n(= (choice) held)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '', 2),
    ('nested', 'he', '(= (coin $n) $n)\n(= (coin $n) (+ $n 1))\n(= (choice) (+ (coin 2) (coin 3)))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[4]\n[24]\n', 0),
    ('nested', 'petta', '(= (coin $n) $n)\n(= (coin $n) (+ $n 1))\n(= (choice) (+ (coin 2) (coin 3)))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '4\n24\n', 0),
    ('numeric-overflow', 'he', '(= (choice) 9223372036854775807)\n(= (choice) 1)\n(= (choice) 2)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[3]\n[9223372036854775810]\n', 0),
    ('numeric-overflow', 'petta', '(= (choice) 9223372036854775807)\n(= (choice) 1)\n(= (choice) 2)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '3\n9223372036854775810\n', 0),
    ('nondeterministic-step', 'he', '(= (choice) 3)\n(= (choice) 3)\n(= (both $a $x) (+ $a $x))\n(= (both $a $x) (- $a $x))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (both $a $x)))\n', '[2]\n[6, 0, 0, -6]\n', 0),
    ('nondeterministic-step', 'petta', '(= (choice) 3)\n(= (choice) 3)\n(= (both $a $x) (+ $a $x))\n(= (both $a $x) (- $a $x))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (both $a $x))\n', '2\n6\n0\n0\n-6\n', 0),
    ('step-effect', 'he', '(= (choice) 3)\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (let $ignored (println! step-effect) (+ $a $x))))\n', 'step-effect\nstep-effect\n[2]\n[8]\n', 0),
    ('step-effect', 'petta', '(= (choice) 3)\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (let $ignored (println! step-effect) (+ $a $x)))\n', 'step-effect\nstep-effect\n2\n8\n', 0),
    ('held-syntax', 'he', '(= (choice) 3)\n!(let $held (collapse (println! held-effect)) (quote $held))\n', 'held-effect\n[(quote (()))]\n', 0),
    ('held-syntax', 'petta', '(= (choice) 3)\n!(let $held (collapse (println! held-effect)) (quote $held))\n', 'held-effect\n(true)\n', 0),
    ('proof-count', 'he', '(= (proof-leaves 0) left)\n(= (proof-leaves 0) right)\n(= (proof-leaves $n) (if (> $n 0) (Node (proof-leaves (- $n 1)) (proof-leaves (- $n 1))) (empty)))\n!(size (collapse (proof-leaves 3)))\n', '[256]\n', 0),
    ('numeric-fold', 'he', '(= (samples $n) (* $n $n))\n(= (samples $n) (if (< $n 1024) (samples (+ $n 1)) (empty)))\n!(let $xs (collapse (samples 0)) (foldl-atom $xs 0 $acc $item (+ $acc $item)))\n', '[358438400]\n', 0),
    ('proof-count', 'petta', '(= (proof-leaves 0) left)\n(= (proof-leaves 0) right)\n(= (proof-leaves $n) (if (> $n 0) (Node (proof-leaves (- $n 1)) (proof-leaves (- $n 1))) (empty)))\n!(length (collapse (proof-leaves 3)))\n', '256\n', 0),
    ('numeric-fold', 'petta', '(= (samples $n) (* $n $n))\n(= (samples $n) (if (< $n 1024) (samples (+ $n 1)) (empty)))\n!(foldl-atom (collapse (samples 0)) 0 $acc $item (+ $acc $item))\n', '358438400\n', 0),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--gmp", choices=("0", "1"), default="1")
    args = parser.parse_args()
    binary = args.binary.resolve()
    failures = []
    observations = 0
    with tempfile.TemporaryDirectory(prefix="owned-consumers-", dir="runtime") as directory:
        for name, language, text, expected, expected_exit in CASES:
            if name == 'numeric-overflow' and args.gmp == '0':
                if language == 'he':
                    expected = '[3]\n[(Error (+ (Error (+ 9223372036854775807 1) BigintArithmeticUnavailable) 2) (BadArgType 1 Number ErrorType))]\n'
                else:
                    expected, expected_exit = '', 2
            source = Path(directory) / (name + "-" + language + ".metta")
            source.write_text(text)
            variants = itertools.product(('0', '1'), repeat=2) if (
                name.startswith(('choice-', 'result-demand-', 'invocation-', 'scope-')) or
                name == 'nested-choice-activation-owner') else [(None, None)]
            for switches in variants:
                environment = dict(os.environ)
                if switches[0] is not None:
                    environment['CETTA_OPEN_EQUATIONS_REFERENCE'] = switches[0]
                    environment['CETTA_PREPARED_PURE_REFERENCE'] = switches[1]
                observations += 1
                # Destination controls use reference PeTTa. Extended binding
                # forms permit authored alternatives and retain their own tests.
                profile = [] if name.startswith(('result-demand-', 'invocation-', 'scope-')) else ["--profile", "extended"]
                result = subprocess.run([str(binary), "--lang", language, *profile,
                    str(source)], capture_output=True, text=True, timeout=30, env=environment)
                if result.returncode != expected_exit or result.stdout != expected:
                    failures.append((name, language, switches, result.returncode, result.stdout, result.stderr))
                elif expected_exit and "uncaught PeTTa error:" not in result.stderr:
                    failures.append((name, language, switches, "missing fault", result.stderr))
                elif name == 'numeric-overflow' and args.gmp == '0' and language == 'petta' and \
                        'BigintArithmeticUnavailable' not in result.stderr:
                    failures.append((name, language, switches, "missing numeric capability fault", result.stderr))
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print("owned resumable consumers: " + str(observations) + " dialect/worker observations passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
