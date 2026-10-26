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
    ('invocation-layout-unary-data', 'petta', '!(car-atom (quote ((+ 1 2) 7)))\n!(cdr-atom (quote ((println! forbidden) 7)))\n', '(+ 1 2)\n(7)\n', 0),
    ('invocation-layout-argument-order', 'petta', '!(exclude-item (let $_ (println! left) 1) (let $_ (println! right) (quote (1 2 1))))\n', 'left\nright\n(2)\n', 0),
    ('invocation-layout-set-duplicates', 'petta', '!(list_to_set (superpose ((quote (1 2 1)) (quote (1 2 1)))))\n', '(1 2)\n(1 2)\n', 0),
    ('invocation-layout-read-code', 'petta', '!(sread "(+ 1 2)")\n', '(+ 1 2)\n', 0),
    ('invocation-layout-predicate-held', 'petta', '(: Predicate (-> Atom Atom))\n!(Predicate (println! forbidden))\n', '(println! forbidden)\n', 0),
    ('invocation-preparation-postcondition-order', 'petta', '(: prep-result (-> Atom Wanted))\n(= (get-type (Tagged $value)) (let $_ (println! checked) Wanted))\n(= (prep-result $value) (let $_ (println! body) (Tagged $value)))\n!(prep-result 3)\n', 'body\nchecked\n(Tagged 3)\n', 0),
    ('invocation-preparation-held-1', 'petta', '(: prep-wide (-> Atom Atom))\n(= (prep-wide $a0) $a0)\n!(prep-wide (println! forbidden))\n', '(println! forbidden)\n', 0),
    ('invocation-preparation-held-8', 'petta', '(: prep-wide (-> Number Number Number Number Number Number Number Atom Atom))\n(= (prep-wide $a0 $a1 $a2 $a3 $a4 $a5 $a6 $a7) $a7)\n!(prep-wide (+ 0 1) (+ 1 1) (+ 2 1) (+ 3 1) (+ 4 1) (+ 5 1) (+ 6 1) (println! forbidden))\n', '(println! forbidden)\n', 0),
    ('invocation-preparation-held-9', 'petta', '(: prep-wide (-> Number Number Number Number Number Number Number Number Atom Atom))\n(= (prep-wide $a0 $a1 $a2 $a3 $a4 $a5 $a6 $a7 $a8) $a8)\n!(prep-wide (+ 0 1) (+ 1 1) (+ 2 1) (+ 3 1) (+ 4 1) (+ 5 1) (+ 6 1) (+ 7 1) (println! forbidden))\n', '(println! forbidden)\n', 0),
    ('invocation-preparation-held-64', 'petta', '(: prep-wide (-> Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Atom Atom))\n(= (prep-wide $a0 $a1 $a2 $a3 $a4 $a5 $a6 $a7 $a8 $a9 $a10 $a11 $a12 $a13 $a14 $a15 $a16 $a17 $a18 $a19 $a20 $a21 $a22 $a23 $a24 $a25 $a26 $a27 $a28 $a29 $a30 $a31 $a32 $a33 $a34 $a35 $a36 $a37 $a38 $a39 $a40 $a41 $a42 $a43 $a44 $a45 $a46 $a47 $a48 $a49 $a50 $a51 $a52 $a53 $a54 $a55 $a56 $a57 $a58 $a59 $a60 $a61 $a62 $a63) $a63)\n!(prep-wide (+ 0 1) (+ 1 1) (+ 2 1) (+ 3 1) (+ 4 1) (+ 5 1) (+ 6 1) (+ 7 1) (+ 8 1) (+ 9 1) (+ 10 1) (+ 11 1) (+ 12 1) (+ 13 1) (+ 14 1) (+ 15 1) (+ 16 1) (+ 17 1) (+ 18 1) (+ 19 1) (+ 20 1) (+ 21 1) (+ 22 1) (+ 23 1) (+ 24 1) (+ 25 1) (+ 26 1) (+ 27 1) (+ 28 1) (+ 29 1) (+ 30 1) (+ 31 1) (+ 32 1) (+ 33 1) (+ 34 1) (+ 35 1) (+ 36 1) (+ 37 1) (+ 38 1) (+ 39 1) (+ 40 1) (+ 41 1) (+ 42 1) (+ 43 1) (+ 44 1) (+ 45 1) (+ 46 1) (+ 47 1) (+ 48 1) (+ 49 1) (+ 50 1) (+ 51 1) (+ 52 1) (+ 53 1) (+ 54 1) (+ 55 1) (+ 56 1) (+ 57 1) (+ 58 1) (+ 59 1) (+ 60 1) (+ 61 1) (+ 62 1) (println! forbidden))\n', '(println! forbidden)\n', 0),
    ('invocation-preparation-held-65', 'petta', '(: prep-wide (-> Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Number Atom Atom))\n(= (prep-wide $a0 $a1 $a2 $a3 $a4 $a5 $a6 $a7 $a8 $a9 $a10 $a11 $a12 $a13 $a14 $a15 $a16 $a17 $a18 $a19 $a20 $a21 $a22 $a23 $a24 $a25 $a26 $a27 $a28 $a29 $a30 $a31 $a32 $a33 $a34 $a35 $a36 $a37 $a38 $a39 $a40 $a41 $a42 $a43 $a44 $a45 $a46 $a47 $a48 $a49 $a50 $a51 $a52 $a53 $a54 $a55 $a56 $a57 $a58 $a59 $a60 $a61 $a62 $a63 $a64) $a64)\n!(prep-wide (+ 0 1) (+ 1 1) (+ 2 1) (+ 3 1) (+ 4 1) (+ 5 1) (+ 6 1) (+ 7 1) (+ 8 1) (+ 9 1) (+ 10 1) (+ 11 1) (+ 12 1) (+ 13 1) (+ 14 1) (+ 15 1) (+ 16 1) (+ 17 1) (+ 18 1) (+ 19 1) (+ 20 1) (+ 21 1) (+ 22 1) (+ 23 1) (+ 24 1) (+ 25 1) (+ 26 1) (+ 27 1) (+ 28 1) (+ 29 1) (+ 30 1) (+ 31 1) (+ 32 1) (+ 33 1) (+ 34 1) (+ 35 1) (+ 36 1) (+ 37 1) (+ 38 1) (+ 39 1) (+ 40 1) (+ 41 1) (+ 42 1) (+ 43 1) (+ 44 1) (+ 45 1) (+ 46 1) (+ 47 1) (+ 48 1) (+ 49 1) (+ 50 1) (+ 51 1) (+ 52 1) (+ 53 1) (+ 54 1) (+ 55 1) (+ 56 1) (+ 57 1) (+ 58 1) (+ 59 1) (+ 60 1) (+ 61 1) (+ 62 1) (+ 63 1) (println! forbidden))\n', '(println! forbidden)\n', 0),
    ('invocation-operands-order', 'petta', '(= (arg-tick $x) (let $_ (println! $x) $x))\n(= (arg-pair $a $b) (let $_ (println! body) (Pair $a $b)))\n!(arg-pair (arg-tick 1) (arg-tick 2))\n', '1\n2\nbody\n(Pair 1 2)\n', 0),
    ('invocation-operands-failure', 'petta', '(= (arg-stop) (empty))\n(= (arg-pair $a $b) (Pair $a $b))\n!(arg-pair (arg-stop) (println! forbidden-late))\n', '', 0),
    ('invocation-operands-completed-code', 'petta', '(= (arg-pair $a $b) (Pair $a $b))\n!(arg-pair (quote (println! forbidden)) 7)\n', '(Pair (println! forbidden) 7)\n', 0),
    ('invocation-operands-later-alias', 'petta', '(= (arg-pin 7) first)\n(= (arg-pair $a $b) (Pair $a $b))\n!(arg-pair (arg-pin $caller) $caller)\n', '(Pair first 7)\n', 0),
    ('invocation-operands-open-scope', 'petta', '(= (arg-id $x) $x)\n(= (arg-pair $a $b) (Pair $a $b))\n(= (arg-outer $x) (arg-pair (arg-id $x) (Pair $x $x)))\n!(let $answer (arg-outer $caller) (Both $caller $answer))\n', '(Both $V0 (Pair $V0 (Pair $V0 $V0)))\n', 0),
    ('invocation-operands-early-error', 'he', '!(Pair (/ 1 0) (println! forbidden-late))\n', '[(Error (/ 1 0) DivisionByZero)]\n', 0),
    ('invocation-operands-held-error', 'he', '!(Pair (quote (Error authored data)) 7)\n', '[(Pair (quote (Error authored data)) 7)]\n', 0),
    ('invocation-operands-width-0', 'petta', '(= (arg-wide ) (Tuple ))\n!(arg-wide )\n', '(Tuple)\n', 0),
    ('invocation-operands-width-8', 'petta', '(= (arg-wide $x0 $x1 $x2 $x3 $x4 $x5 $x6 $x7) (Tuple $x0 $x1 $x2 $x3 $x4 $x5 $x6 $x7))\n!(arg-wide (+ 0 1) (+ 1 1) (+ 2 1) (+ 3 1) (+ 4 1) (+ 5 1) (+ 6 1) (+ 7 1))\n', '(Tuple 1 2 3 4 5 6 7 8)\n', 0),
    ('invocation-operands-width-9', 'petta', '(= (arg-wide $x0 $x1 $x2 $x3 $x4 $x5 $x6 $x7 $x8) (Tuple $x0 $x1 $x2 $x3 $x4 $x5 $x6 $x7 $x8))\n!(arg-wide (+ 0 1) (+ 1 1) (+ 2 1) (+ 3 1) (+ 4 1) (+ 5 1) (+ 6 1) (+ 7 1) (+ 8 1))\n', '(Tuple 1 2 3 4 5 6 7 8 9)\n', 0),
    ('invocation-sequence-open-scope', 'petta', '(= (local $n) (let (Pair $x) (Pair $n) (Pair $n $x)))\n!(let $answer (local $caller) (Both $caller $answer))\n', '(Both $V0 (Pair $V0 $V0))\n', 0),
    ('invocation-sequence-separate-scopes', 'petta', '(= (local $n) (let (Pair $x) (Pair $n) (Pair $n $x)))\n!(let $a (local $left) (let $b (local $right) (Both $left $a $right $b)))\n', '(Both $V0 (Pair $V0 $V0) $V1 (Pair $V1 $V1))\n', 0),
    ('invocation-sequence-let-root', 'petta', '(= (pattern $x) (let $_ (println! binder) (Pair $x)))\n(= (produce) (let $_ (println! producer) (Pair 7)))\n!(let (pattern $x) (produce) (let $_ (println! body) $x))\n', 'binder\nproducer\nbody\n7\n', 0),
    ('invocation-sequence-chain-root', 'petta', '(= (pattern $x) (let $_ (println! binder) (Pair $x)))\n(= (produce) (let $_ (println! producer) (Pair 7)))\n!(chain (produce) (pattern $x) (let $_ (println! body) $x))\n', 'producer\nbinder\nbody\n7\n', 0),
    ('invocation-sequence-let-equation', 'petta', '(= (pattern $x) (let $_ (println! binder) (Pair $x)))\n(= (produce) (let $_ (println! producer) (Pair 7)))\n(= (run) (let (pattern $x) (produce) (let $_ (println! body) $x)))\n!(run)\n', 'binder\nproducer\nbody\n7\n', 0),
    ('invocation-sequence-chain-equation', 'petta', '(= (pattern $x) (let $_ (println! binder) (Pair $x)))\n(= (produce) (let $_ (println! producer) (Pair 7)))\n(= (run) (chain (produce) (pattern $x) (let $_ (println! body) $x)))\n!(run)\n', 'producer\nbinder\nbody\n7\n', 0),
    ('invocation-case-failure', 'petta', '(= (only-nil $x) (case $x ((Nil 7))))\n!(only-nil (Cons 1 Nil))\n!(only-nil Nil)\n', '7\n', 0),
    ('invocation-oem-prepared-failure', 'petta', '(= (descend $n $x) (if (== $n 0) (case $x ((Nil 7))) (descend (- $n 1) $x)))\n(= (r $x) 0)\n(= (r $x) (descend 3 $x))\n!(r (Cons 1 Nil))\n!(r Nil)\n', '0\n0\n7\n', 0),
    ('invocation-oem-completed-first', 'petta', '(: g (-> Atom Atom))\n(= (g $v) (min 3 1))\n!(g 5)\n', '(min 3 1)\n', 0),
    ('invocation-oem-completed-resumed', 'petta', '(: g (-> Atom Atom))\n(= (g $v) (min 3 1))\n(= (r $n) 0)\n(= (r $n) (g 5))\n(= (r $n) (g 5))\n!(r 2)\n', '0\n(min 3 1)\n(min 3 1)\n', 0),
    ('invocation-oem-completed-host', 'petta', '(: g (-> Atom Atom))\n(= (g $v) (min 3 1))\n(= (r $n) 0)\n(= (r $n) (let $_ (println! once) (g 5)))\n!(r 2)\n', 'once\n0\n(min 3 1)\n', 0),
    ('invocation-oem-completed-control-data', 'petta', '(: g (-> Atom Atom))\n(= (g 1) (empty))\n(= (g 2) (Error payload Data))\n(= (g 3) (println! forbidden))\n(= (r $n) 0)\n(= (r $n) (g $n))\n!(r 1)\n!(r 2)\n!(r 3)\n', '0\n(empty)\n0\n(Error payload Data)\n0\n(println! forbidden)\n', 0),
    ('prepared-failure-readback-map', 'he', '!(eval (_minimal-map-atom (1 2) $x (+ $x 1)))\n', '[(return (2 3))]\n', 0),
    ('prepared-failure-readback-code', 'he', '!(eval (_minimal-map-atom ((println! forbidden)) $x $x))\n', '[(return ((println! forbidden)))]\n', 0),
    ('prepared-failure-readback-run', 'he', '!(function (eval (_minimal-map-atom ((println! forbidden)) $x $x)))\n', '[((println! forbidden))]\n', 0),
    ('prepared-failure-readback-sum', 'he', '(= (project $x) $x)\n!(eval (_minimal-sum-by (1 2) project))\n', '[3]\n', 0),
    ('prepared-failure-cache-reuse', 'petta', '(= (miss $p) (let (Pair $x $y) $p 7))\n!(miss (Single 3))\n!(miss (Pair 4 5))\n!(miss (Single 8))\n!(miss (Pair 6 7))\n', '7\n7\n', 0),
    ('prepared-failure-cache-reuse', 'he', '(= (miss $p) (let (Pair $x $y) $p 7))\n!(miss (Single 3))\n!(miss (Pair 4 5))\n!(miss (Single 8))\n!(miss (Pair 6 7))\n', '[7]\n[7]\n', 0),
    ('prepared-failure-argument-short-circuit', 'petta', '(= (miss $p) (let (Pair $x $y) $p 7))\n(= (touch $x) (let $_ (println! forbidden) $x))\n(= (pair $a $b) (Pair $a $b))\n!(pair (miss (Single 3)) (touch 2))\n', '', 0),
    ('prepared-failure-argument-short-circuit', 'he', '(= (miss $p) (let (Pair $x $y) $p 7))\n(= (touch $x) (let $_ (println! forbidden) $x))\n(= (pair $a $b) (Pair $a $b))\n!(pair (miss (Single 3)) (touch 2))\n', '', 0),
    ('prepared-failure-condition-short-circuit', 'petta', '(= (miss $p) (let (Pair $x $y) $p 7))\n(= (branch $p) (if (miss $p) (println! forbidden-then) (println! forbidden-else)))\n(= (branch $p) fallback)\n!(branch (Single 3))\n', 'fallback\n', 0),
    ('prepared-failure-condition-short-circuit', 'he', '(= (miss $p) (let (Pair $x $y) $p 7))\n(= (branch $p) (if (miss $p) (println! forbidden-then) (println! forbidden-else)))\n(= (branch $p) fallback)\n!(branch (Single 3))\n', '[fallback]\n', 0),
    ('prepared-failure-code-data', 'petta', '(= (carry $x) $x)\n!(carry (quote (empty)))\n!(carry Empty)\n', '(empty)\nEmpty\n', 0),
    ('prepared-failure-raised-fallback', 'petta', '(= (fraction $x) (/ 1 $x))\n!(car-atom (catch (fraction 0)))\n', 'Error\n', 0),
    ('prepared-failure-mutation', 'petta', '(= (miss $p) (let (Pair $x $y) $p 7))\n!(miss (Single 3))\n!(let $_ (add-atom &self (= (miss $p) 8)) (miss (Single 3)))\n', '8\n', 0),
    ('prepared-failure-mutation', 'he', '(= (miss $p) (let (Pair $x $y) $p 7))\n!(miss (Single 3))\n!(let $_ (add-atom &self (= (miss $p) 8)) (miss (Single 3)))\n', '[8]\n', 0),
    ('source-plan-completed-code', 'petta', '(= (emit $x) (println! forbidden))\n!(let $v (quote (emit 7)) $v)\n', '(emit 7)\n', 0),
    ('source-plan-constructor-fields', 'petta', '(= (emit $x) (let $_ (println! child) $x))\n!(Packet (emit 3) (quote (emit 4)))\n', 'child\n(Packet 3 (emit 4))\n', 0),
    ('source-plan-constructor-authority', 'petta', '(= (build $x) (Packet $x))\n!(let $_ (add-atom &self (= (Packet $x) (println! forbidden))) (build 7))\n', '(Packet 7)\n', 0),
    ('source-plan-call-retirement', 'petta', '(= (known $x) (Pair $x))\n(= (run $x) (known $x))\n!(let $_ (remove-atom &self (= (known $x) (Pair $x))) (run 7))\n', '(known 7)\n', 0),
    ('scope-caller-alternatives', 'petta', '(= (inner) (let $_ (cut) 1))\n(= (inner) 2)\n(= (outer) (let $before (superpose (a b)) (Pair $before (inner))))\n!(outer)\n', '(Pair a 1)\n(Pair b 1)\n', 0),
    ('scope-earlier-subgoal-and-later-duplicates', 'petta', '(= (inner) (superpose (1 2)))\n(= (outer) (let $x (inner) (let $_ (cut) (let $later (superpose (7 7)) (Pair $x $later)))))\n(= (outer) discarded)\n!(outer)\n', '(Pair 1 7)\n(Pair 1 7)\n', 0),
    ('scope-handler-transparency', 'petta', '(= (outer) (catch (let $_ (cut) (/ 1 0))))\n(= (outer) 9)\n!(let $answers (collapse (outer)) (size-atom $answers))\n', '1\n', 0),
    ('scope-committed-failure', 'petta', '(= (outer) (let $_ (println! entered) (let $_ (cut) (empty))))\n(= (outer) (println! forbidden))\n!(collapse (outer))\n', 'entered\n()\n', 0),
    ('scope-cut-shaped-data', 'petta', '(= (outer) (let $v (quote (cut)) (let $_ $v 1)))\n(= (outer) 2)\n!(collapse (outer))\n', '(1 2)\n', 0),
    ('scope-once-restores-caller-scope', 'petta', '(= (outer) (let $x (once (superpose (1 2))) (let $_ (cut) $x)))\n(= (outer) 9)\n!(collapse (outer))\n', '(1)\n', 0),
    ('scope-callee-return-restores-scope', 'petta', '(= (inner) (superpose (1 2)))\n(= (outer) (let $x (inner) (let $_ (cut) $x)))\n(= (outer) 9)\n!(collapse (outer))\n', '(1)\n', 0),
    ('scope-handled-error-keeps-new-choices', 'petta', '(= (outer) (let $_ (catch (/ 1 0)) (superpose (7 7))))\n(= (outer) 9)\n!(collapse (outer))\n', '(7 7 9)\n', 0),
    ('invocation-captured-data', 'petta', '(= (transport $a $b $c) (Bundle $a $b $c))\n!(let $p (transport (quote (println! forbidden))) ($p 7 9))\n', '(Bundle (println! forbidden) 7 9)\n', 0),
    ('forcing-meaning', 'petta', '(= (pass $x) $x)\n!(let $c (quote (pass (+ 1 2))) (eval $c))\n!(let $c (quote (pass (+ 1 2))) (reduce $c))\n', '3\n(+ 1 2)\n', 0),
    ('forcing-effects', 'petta', '(= (pass $x) $x)\n(= (tick) (let $_ (println! tick) 7))\n!(let $c (quote (pass (tick))) (eval $c))\n!(let $c (quote (pass (tick))) (reduce $c))\n', 'tick\n7\n(tick)\n', 0),
    ('forcing-duplicates', 'petta', '(= (pass $x) $x)\n!(let $c (quote (superpose (1 1 2))) (collapse (eval $c)))\n!(let $c (quote (pass (superpose (1 1 2)))) (collapse (reduce $c)))\n', '(1 1 2)\n((superpose (1 1 2)))\n', 0),
    ('forcing-caught', 'petta', '(= (pass $x) $x)\n!(let $c (quote (/ 1 0)) (let $e (catch (eval $c)) (car-atom $e)))\n!(let $c (quote (pass (/ 1 0))) (reduce $c))\n', 'Error\n(/ 1 0)\n', 0),
    ('forcing-dynamic', 'petta', '(= (pass $x) $x)\n!(let $f eval ($f (quote (pass (+ 1 2)))))\n!(let $f reduce ($f (quote (pass (+ 1 2)))))\n', '3\n(+ 1 2)\n', 0),
    ('forcing-explicit-quote', 'petta', '(= (pass $x) $x)\n!(eval (quote (pass (+ 1 2))))\n!(reduce (quote (pass (+ 1 2))))\n', '(pass (+ 1 2))\n(quote 3)\n', 0),
    ('forcing-empty', 'petta', '(= (pass $x) $x)\n!(let $c (quote (superpose ())) (collapse (eval $c)))\n!(let $c (quote (pass (superpose ()))) (collapse (reduce $c)))\n', '()\n((superpose ()))\n', 0),
    ('forcing-constructor', 'petta', '!(let $c (quote (Constructor (+ 1 2))) (eval $c))\n!(let $c (quote (Constructor (+ 1 2))) (reduce $c))\n', '(Constructor 3)\n(Constructor (+ 1 2))\n', 0),
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
                name.startswith(('choice-', 'result-demand-', 'invocation-', 'scope-', 'source-plan-', 'prepared-failure-', 'forcing-')) or
                name == 'nested-choice-activation-owner') else [(None, None)]
            for switches in variants:
                environment = dict(os.environ)
                if switches[0] is not None:
                    environment['CETTA_OPEN_EQUATIONS_REFERENCE'] = switches[0]
                    environment['CETTA_PREPARED_PURE_REFERENCE'] = switches[1]
                observations += 1
                # Destination controls use reference PeTTa. Extended binding
                # forms permit authored alternatives and retain their own tests.
                profile = [] if name.startswith(('result-demand-', 'invocation-', 'scope-', 'source-plan-', 'prepared-failure-', 'forcing-')) else ["--profile", "extended"]
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
