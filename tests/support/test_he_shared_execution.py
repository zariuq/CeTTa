#!/usr/bin/env python3
"""Ordered HE observations at compiled and raw continuation boundaries."""
import argparse
import math
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


# The last field requires the shared machine. Intrinsic demands and closed
# calls can select their existing qualified realizations.
CASES = [
    ('tuple-constructed-error', '!(metta (a Error) %Undefined% &self)\n', '[(Error)]\n', False),
    ('tuple-child-error', '!(metta (a (Error)) %Undefined% &self)\n', '[(Error)]\n', False),
    ('tuple-rightmost-error',
     '!(metta (a Error b Error c) %Undefined% &self)\n',
     '[(Error c)]\n',
     False),
    ('tuple-nested-error', '!(metta (a (b Error)) %Undefined% &self)\n', '[(Error)]\n', False),
    ('tuple-singleton-wrapper', '!(metta (a) %Undefined% &self)\n', '[(a)]\n', False),
    ('tuple-suffix-effect',
     '!(metta (a Error (let $u (println! tail) z)) %Undefined% &self)\n',
     'tail\n[(Error z)]\n',
     False),
    ('tuple-child-error-short-circuit',
     '!(metta (a (Error bad code) (println! unexpected)) %Undefined% &self)\n',
     '[(Error bad code)]\n',
     False),
    ('tuple-raw-argument',
     '(: held (-> Atom Atom))\n'
     '(= (held $x) $x)\n'
     '!(held (a Error))\n'
     '!(held (a Error (println! unexpected)))\n',
     '[(a Error)]\n[(a Error (println! unexpected))]\n',
     False),
    ('tuple-late-error-binding',
     '(= (bind Error) tail)\n!(metta (a $x (bind $x)) %Undefined% &self)\n',
     '[(Error tail)]\n',
     False),
    ('tuple-private-error-binding',
     '(= (bind Error) tail)\n(= (tuple) (a $x (bind $x)))\n!(metta (tuple) %Undefined% &self)\n',
     '[(Error tail)]\n',
     False),
    ('tuple-duplicate-errors',
     '(= (choose) Error)\n(= (choose) Error)\n!(metta (a (choose) z) %Undefined% &self)\n',
     '[(Error z), (Error z)]\n',
     False),
    ('tuple-error-alternative-filter',
     '(= (choose) Error)\n'
     '(= (choose) Error)\n'
     '(= (choose) keep)\n'
     '!(metta (a (choose) z) %Undefined% &self)\n',
     '[(a keep z)]\n',
     False),
    ('private-name-sharing', '(= (one) (Box $hole))\n!(let $a (one) (let $as (cons-atom $a ()) (== (format-args "{}" $as) (format-args "{}" $as))))\n', '[True]\n', False),
    ('private-name-distinctness', '(= (one) (Box $hole))\n!(let $a (one) (let $b (one) (let $as (cons-atom $a ()) (let $bs (cons-atom $b ()) (== (format-args "{}" $as) (format-args "{}" $bs))))))\n', '[False]\n', False),
    ('private-looking-literal', '! "literal $hole#17"\n', '["literal $hole#17"]\n', False),
    ('raw-expansion-after-failure', '(= (miss $p) (let (Pair $x $y) $p 7))\n!(miss (Single 3))\n!(eval (_minimal-map-atom (1 2) $x (return $x)))\n', '[(chain (eval (size-atom (1 2))) $size#0 (eval (if-equal $size#0 0 (return (1 2)) (chain (decons-atom (1 2)) $ht#0 (unify ($head#0 $tail#0) $ht#0 (chain (eval (sealed ($x) (return $x))) $sealedmap#0 (chain (eval (map-atom $tail#0 $x $sealedmap#0)) $tail-mapped#0 (chain (eval (atom-subst $head#0 $x $sealedmap#0)) $map-expr#0 (chain $map-expr#0 $head-mapped#0 (chain (cons-atom $head-mapped#0 $tail-mapped#0) $res#0 (return $res#0)))))) (empty))))))]\n', False),
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
     "[(box $caller $hole#0)]\n", False),
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
     "(= (boxed) (Box (+ 1 \"S\") (loop)))\n!(boxed)\n",
     '[(Error (+ 1 "S") (BadArgType 2 Number String))]\n', True),
    # A closed data tuple at the root computes only in its fields, each at
    # its own call boundary.
    ("root-constructor-fault-before-divergence", "(= (loop) (loop))\n"
     "!(Box (+ 1 \"S\") (loop))\n",
     '[(Error (+ 1 "S") (BadArgType 2 Number String))]\n', False),
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
     "[(2 42)]\n[()]\n[(2)]\n", False),
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
    ("chain-result-replaces-occupied-slot",
     "!(function (chain (unify $x A B refused) $x (return $x)))\n",
     "[B]\n", False),
    ("chain-result-distinct-slot",
     "!(function (chain (unify $x A B refused) $y (return $y)))\n",
     "[B]\n", False),
    ("chain-result-repeated-slot",
     "!(function (chain (unify $x A B refused) $x (return (pair $x $x))))\n",
     "[(pair B B)]\n", False),
    ("chain-retains-other-child-bindings",
     "!(function (chain (unify $x A B refused) $y (return (pair $x $y))))\n",
     "[(pair A B)]\n", False),
    ("chain-result-replaces-aliased-slot",
     "!(function (chain (unify $x $z B refused) $x (return $x)))\n",
     "[B]\n", False),
    ("chain-retains-nested-source-slot",
     "!(function (chain (unify $x A B refused) $r (chain C $x (return $x))))\n",
     "[C]\n", False),
    ("chain-unify-conflicting-child-bindings",
     "!(function (chain (unify $x A first refused) $r "
     "(chain (unify $x B accepted refused) $s (return $s))))\n",
     "[refused]\n", False),
    ("chain-unify-compatible-child-bindings",
     "!(function (chain (unify $x A first refused) $r "
     "(chain (unify $x A accepted refused) $s (return (pair $x $s)))))\n",
     "[(pair A accepted)]\n", False),
    ("minimal-no-return", "!(function (chain (eval (+ 1 2)) $x $x))\n",
     "[(Error (function (chain (eval (+ 1 2)) $x $x)) NoReturn)]\n", False),
    ("minimal-raw-result-stays-syntax", "(= (raw-step) (+ 1 2))\n"
     "!(eval (raw-step))\n", "[(+ 1 2)]\n", False),
    ("minimal-returned-effect-stays-data", "!(function (return (println! must-stay-data)))\n",
     "[(println! must-stay-data)]\n", False),
    ("minimal-chain-keeps-duplicates", "!(function "
     "(chain (eval (superpose (7 7 8))) $dst (return $dst)))\n",
     "[7, 7, 8]\n", False),
    ("minimal-raw-delimiter-completes", "(= (code) (function (return (+ 1 2))))\n"
     "!(eval (code))\n", "[(+ 1 2)]\n", False),
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
     "first\nsecond\n[(), ()]\n", False),
    ("bind-completed-producer", "(= (producer) (let $u (println! first) 1))\n"
     "(= (producer) (let $u (println! second) 2))\n"
     "!(bind! &kept (producer))\n! &kept\n", "first\nsecond\n[()]\n[1]\n", False),
    ("captured-registry-operand-types", "!(bind! &typed-number 5)\n"
     "(= (compared $x) (== $x &typed-number))\n"
     "(: observed (-> Atom %Undefined%))\n"
     "(= (observed $v) (case $v (((Error $a $b) (error $b)) ($x $x))))\n"
     "!(observed (compared 5))\n!(observed (compared \"s\"))\n",
     '[()]\n[True]\n[(error (BadArgType 2 String Number))]\n', False),
    ("collapse-bind-aliases", "(= (row 1) a)\n(= (row 1) a)\n(= (row 2) b)\n"
     "!(let $pairs (collapse-bind (row $k)) "
     "(let $v (superpose-bind $pairs) ($k $v)))\n",
     "[(1 a), (1 a), (2 b)]\n", False),
    ("minimal-capture-keeps-caller-alias", "(= (row 1) a)\n"
     "(= (row 1) a)\n(= (row 2) b)\n"
     "!(let $pairs (collapse-bind (eval (row $key))) "
     "(let $value (superpose-bind $pairs) ($key $value)))\n",
     "[(1 a), (1 a), (2 b)]\n", False),
    ("minimal-capture-through-wrapper", "(= (row 1) a)\n"
     "(= (row 1) a)\n(= (row 2) b)\n"
     "(= (restore $pairs) (superpose-bind $pairs))\n"
     "!(let $pairs (collapse-bind (eval (row $key))) "
     "(let $value (restore $pairs) ($key $value)))\n",
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


# Source-certified native constructors execute at the host.
# Negative constructor occurrences retain their canonical host. Inert
# children may still select an independently admitted shared realization.
CASES.extend([
    (
        'native-space-reread-completes-once',
        '(= (constructor-target $x) (+ $x 1))\n'
        '(= (constructor-codes) (let $drop (new-space) constructor-target))\n'
        '(= (constructor-codes) 7)\n'
        '(= (constructor-reread) (let $bag (reify (constructor-codes)) $bag))\n'
        '!(constructor-reread)\n'
        '!(+ 20 22)\n',
        '[8]\n[42]\n',
        True,
    ),
    (
        'native-space-retained-alias',
        '(= (fresh-space) (new-space))\n'
        '(= (carry-space $n $s) (if (== $n 0) $s\n'
        '  (let $drop (new-space) (carry-space (- $n 1) $s))))\n'
        '(= (kept-space $n) (let $s (new-space) (carry-space $n $s)))\n'
        '!(bind! &a (kept-space 32))\n'
        '!(bind! &alias &a)\n'
        '!(add-atom &alias first)\n'
        '!(get-atoms &a)\n'
        '!(bind! &b (fresh-space))\n'
        '!(get-atoms &b)\n'
        '!(add-atom &b second)\n'
        '!(get-atoms &a)\n'
        '!(get-atoms &b)\n'
        '!(+ 20 22)\n',
        '[()]\n'
        '[()]\n'
        '[()]\n'
        '[first]\n'
        '[()]\n'
        '[()]\n'
        '[first]\n'
        '[second]\n'
        '[42]\n',
        True,
    ),
    (
        'native-space-duplicates',
        '(= (constructor-choice) (let $drop (new-space) left))\n'
        '(= (constructor-choice) (let $drop (new-space) left))\n'
        '(= (constructor-choice) (let $drop (new-space) right))\n'
        '!(constructor-choice)\n'
        '!(+ 20 22)\n',
        '[left, left, right]\n'
        '[42]\n',
        True,
    ),
    (
        'native-space-exact-effects',
        '(= (constructor-effects $n)\n'
        ' (if (== $n 0) done\n'
        '  (let $drop (new-space)\n'
        '   (let $write (add-atom &self (constructor-tick $n))\n'
        '    (constructor-effects (- $n 1))))))\n'
        '!(constructor-effects 3)\n'
        '!(match &self (constructor-tick $n) $n)\n'
        '!(+ 20 22)\n',
        '[done]\n'
        '[3, 2, 1]\n'
        '[42]\n',
        True,
    ),
    (
        'native-space-error-prefix',
        '(= (constructor-error)\n'
        ' (let $drop (new-space)\n'
        '  (let $write (add-atom &self (constructor-before-error 7))\n'
        '   (+ 1 "S"))))\n'
        '!(collapse (constructor-error))\n'
        '!(match &self (constructor-before-error $n) $n)\n'
        '!(+ 20 22)\n',
        '[((Error (+ 1 "S") (BadArgType 2 Number String)))]\n'
        '[7]\n'
        '[42]\n',
        True,
    ),
    (
        'native-space-nr-barrier',
        '(= (constructor-nr-leaf) NotReducible)\n'
        '(= (constructor-nr $n)\n'
        ' (if (== $n 0) (constructor-nr-leaf)\n'
        '  (let $drop (new-space) (constructor-nr (- $n 1)))))\n'
        '!(constructor-nr 3)\n'
        '!(+ 20 22)\n',
        '[(constructor-nr-leaf)]\n'
        '[42]\n',
        False,
    ),
    (
        'native-space-type-barrier',
        '(: constructor-typed (-> Number Number))\n'
        '(= (constructor-typed $n)\n'
        ' (if (== $n 0) True\n'
        '  (let $drop (new-space) (constructor-typed (- $n 1)))))\n'
        '!(constructor-typed 3)\n',
        '[(Error True (BadType Number Bool))]\n',
        False,
    ),
    (
        'native-space-open-barrier',
        '(= (constructor-open $n $value)\n'
        ' (if (== $n 0) (Pair $value kept)\n'
        '  (let $drop (new-space) (constructor-open (- $n 1) $value))))\n'
        '!(constructor-open 3 $visible)\n'
        '!(+ 20 22)\n',
        '[(Pair $visible kept)]\n'
        '[42]\n',
        False,
    ),
    (
        'native-space-arity-barrier',
        '(= (constructor-arity)\n'
        ' (let $drop (new-space set) done))\n'
        '!(constructor-arity)\n'
        '!(+ 20 22)\n',
        '[(Error (new-space set) IncorrectNumberOfArguments)]\n',
        False,
    ),
    (
        'native-space-native-type-authority',
        '(= (constructor-signature)\n'
        ' (let $drop (new-space) done))\n'
        '!(get-type new-space)\n'
        '!(add-atom &self (: new-space (-> Number)))\n'
        '!(get-type new-space)\n'
        '!(match &self (: new-space $type) $type)\n'
        '!(constructor-signature)\n'
        '!(+ 20 22)\n',
        '[(-> SpaceType)]\n'
        '[()]\n'
        '[(-> SpaceType)]\n'
        '[(-> Number)]\n'
        '[done]\n'
        '[42]\n',
        True,
    ),
    (
        'native-space-definition-barrier',
        '(= (constructor-definition)\n'
        ' (let $drop (new-space) done))\n'
        '!(add-atom &self (= (new-space) supplied))\n'
        '!(constructor-definition)\n'
        '!(+ 20 22)\n',
        '[()]\n'
        '[done]\n'
        '[42]\n',
        False,
    ),
    (
        'native-state-stays-canonical',
        '(= (constructor-state)\n'
        ' (let $s (new-state 7) (get-state $s)))\n'
        '!(constructor-state)\n'
        '!(+ 20 22)\n',
        '[7]\n'
        '[42]\n',
        False,
    ),
    (
        'native-state-drop-loop',
        '(= (drop-state $n) (if (== $n 0) done\n'
        ' (let $drop (new-state $n) (drop-state (- $n 1)))))\n'
        '!(drop-state 32)\n!(+ 20 22)\n',
        '[done]\n[42]\n',
        True,
    ),
    (
        'native-state-retained-alias',
        '(= (carry-state $n $s) (if (== $n 0) $s\n'
        ' (let $drop (new-state $n) (carry-state (- $n 1) $s))))\n'
        '(= (kept-state $n) (let $s (new-state 7) (carry-state $n $s)))\n'
        '(= (fresh-state) (new-state 8))\n'
        '!(bind! &a (kept-state 32))\n'
        '!(bind! &alias &a)\n'
        '!(change-state! &alias 9)\n'
        '!(get-state &a)\n'
        '!(bind! &b (fresh-state))\n'
        '!(get-state &b)\n'
        '!(get-state &a)\n',
        '[()]\n[()]\n[(State 9)]\n[9]\n[()]\n[8]\n[9]\n',
        True,
    ),
    (
        'native-state-payload-stays-syntax',
        '(= (state-syntax) (new-state (+ 1 2)))\n'
        '(= (state-error-syntax) (new-state (+ 1 "S")))\n'
        '(= (state-nested-syntax) (new-state (new-state 7)))\n'
        '!(get-state (state-syntax))\n'
        '!(let $drop (state-error-syntax) done)\n'
        '!(get-state (state-nested-syntax))\n',
        '[(+ 1 2)]\n[done]\n[(new-state 7)]\n',
        True,
    ),
    (
        'native-state-reread-completes-once',
        '(= (state-target $x) (+ $x 1))\n'
        '(= (state-codes) (let $drop (new-state 7) state-target))\n'
        '(= (state-codes) 7)\n'
        '(= (state-reread) (let $bag (reify (state-codes)) $bag))\n'
        '!(state-reread)\n!(+ 20 22)\n',
        '[8]\n[42]\n',
        True,
    ),
    (
        'native-state-duplicates',
        '(= (state-choice) (let $drop (new-state 1) left))\n'
        '(= (state-choice) (let $drop (new-state 2) left))\n'
        '(= (state-choice) (let $drop (new-state 3) right))\n'
        '!(state-choice)\n!(+ 20 22)\n',
        '[left, left, right]\n[42]\n',
        True,
    ),
    (
        'native-state-error-prefix',
        '(= (state-error) (let $drop (new-state 7)\n'
        ' (let $write (add-atom &self (state-before-error 7)) (+ 1 "S"))))\n'
        '!(collapse (state-error))\n'
        '!(match &self (state-before-error $x) $x)\n'
        '!(+ 20 22)\n',
        '[((Error (+ 1 "S") (BadArgType 2 Number String)))]\n[7]\n[42]\n',
        True,
    ),
    (
        'native-state-nullary-barrier',
        '(= (state-arity) (let $drop (new-state) done))\n'
        '!(state-arity)\n!(+ 20 22)\n',
        '[(Error (new-state) IncorrectNumberOfArguments)]\n',
        False,
    ),
    (
        'native-state-extra-argument-barrier',
        '(= (state-arity) (let $drop (new-state 7 8) done))\n'
        '!(state-arity)\n!(+ 20 22)\n',
        '[(Error (new-state 7 8) IncorrectNumberOfArguments)]\n',
        False,
    ),
    (
        'native-state-type-barrier',
        '(: state-typed (-> Number Number))\n'
        '(= (state-typed $n) (if (== $n 0) True\n'
        ' (let $drop (new-state $n) (state-typed (- $n 1)))))\n'
        '!(state-typed 3)\n',
        '[(Error True (BadType Number Bool))]\n',
        False,
    ),
    (
        'native-state-fresh-variable-barrier',
        '(= (state-fresh) (new-state $hole))\n'
        '!(get-state (state-fresh))\n!(+ 20 22)\n',
        '[$hole]\n[42]\n',
        False,
    ),
    (
        'native-state-payload-resource-barrier',
        '(= (state-payload-effect)\n'
        ' (let $drop (new-state (add-atom &self (not-executed 7))) done))\n'
        '!(state-payload-effect)\n'
        '!(match &self (not-executed $x) $x)\n!(+ 20 22)\n',
        '[done]\n[42]\n',
        False,
    ),
    (
        'native-state-definition-invalidation',
        '(= (state-definition) (let $drop (new-state 7) done))\n'
        '!(state-definition)\n'
        '!(add-atom &self (= (new-state $value) supplied))\n'
        '!(state-definition)\n!(+ 20 22)\n',
        '[done]\n[()]\n[done]\n[42]\n',
        True,
    ),
    (
        'native-state-signature-invalidation',
        '(= (state-signature) (let $drop (new-state 7) done))\n'
        '!(state-signature)\n'
        '!(add-atom &self (: new-state (-> Number Bool)))\n'
        '!(state-signature)\n!(+ 20 22)\n',
        '[done]\n[()]\n[done]\n[42]\n',
        True,
    ),
])

# Ready State writes retain their complete native demand and publication.
CASES.extend([
    ('native-state-write-overwrite-alias', '(= (overwrite-state $n $s) (if (== $n 0) $s (let $write (change-state! $s $n) (overwrite-state (- $n 1) $s))))\n(= (start-overwrite $n) (let $s (new-state 0) (overwrite-state $n $s)))\n!(bind! &a (start-overwrite 32))\n!(bind! &alias &a)\n!(get-state &a)\n!(change-state! &alias 9)\n!(get-state &a)\n', '[()]\n[()]\n[1]\n[(State 9)]\n[9]\n', True),
    ('native-state-write-old-read', '(= (make-and-write) (let $s (new-state 0) (let $w (change-state! $s 7) $s)))\n!(let $s (make-and-write) (let $old (get-state $s) (let $w (change-state! $s 8) (Pair $old (get-state $s)))))\n', '[(Pair 7 8)]\n', True),
    ('native-state-write-type-error', '(= (state-write-type) (let $s (new-state 7) (change-state! $s "S")))\n!(state-write-type)\n', '[(Error (change-state! (State 7) "S") (BadArgType 2 Number String))]\n', True),
    ('native-state-write-reference-error', '(= (state-write-bad-ref $s) (change-state! $s 9))\n!(state-write-bad-ref 7)\n', '[(Error (change-state! 7 9) (BadArgType 1 (StateMonad $$__state#0) Number))]\n', True),
    ('native-state-write-computed-value-barrier', '(= (state-write-computed) (let $s (new-state 0) (change-state! $s (+ 1 2))))\n!(get-state (state-write-computed))\n', '[3]\n', False),
    ('native-state-write-computed-reference-barrier', '(= (state-write-ref) (change-state! (new-state 0) 7))\n!(get-state (state-write-ref))\n', '[7]\n', False),
    ('native-state-write-error-prefix', '(= (state-write-error) (let $s (new-state 0) (let $w (change-state! $s 7) (let $effect (add-atom &self (write-prefix 7)) (+ 1 "S")))))\n!(collapse (state-write-error))\n!(match &self (write-prefix $x) $x)\n!(+ 20 22)\n', '[((Error (+ 1 "S") (BadArgType 2 Number String)))]\n[7]\n[42]\n', True),
    ('native-state-write-nondeterminism-barrier', '(= (write-values) 1)\n(= (write-values) 2)\n(= (state-write-values) (let $s (new-state 0) (let $v (write-values) (change-state! $s $v))))\n!(state-write-values)\n', '[(State 2), (State 2)]\n', False),
    ('native-state-write-arity-barrier', '(= (state-write-arity) (let $s (new-state 0) (change-state! $s)))\n!(state-write-arity)\n', '[(Error (change-state! (State 0)) IncorrectNumberOfArguments)]\n', False),
    ('native-state-write-caller-old-image', '(= (write-existing $s $value) (change-state! $s $value))\n!(let $s (new-state "old-image") (let $old (get-state $s) (let $changed (write-existing $s "new-image") (Pair $old (get-state $s)))))\n', '[(Pair "old-image" "new-image")]\n', True),
    ('native-state-readback-barrier', '(= (state-readback) (let $s (new-state (+ 1 2)) (let $value (get-state $s) $value)))\n!(state-readback)\n', '[3]\n', False),
    ('native-state-write-definition-invalidation', '(= (write-definition) (let $s (new-state 0) (change-state! $s 7)))\n!(get-state (write-definition))\n!(add-atom &self (= (change-state! $s $v) supplied))\n!(get-state (write-definition))\n', '[7]\n[()]\n[7]\n', True),
    ('native-state-write-native-type-authority', '(= (write-signature) (let $s (new-state 0) (change-state! $s 7)))\n!(get-state (write-signature))\n!(add-atom &self (: change-state! (-> Number Number Number)))\n!(get-state (write-signature))\n', '[7]\n[()]\n[7]\n', True),
])

# Each region requires only the native operations it actually uses.
CASES.extend([
    ('native-state-write-repeat-authority', '(= (repeat-state) (new-state 7))\n(= (repeat-write) (let $s (new-state 0) (change-state! $s 8)))\n!(let $s (repeat-state) (get-metatype $s))\n!(let $s (repeat-state) (get-metatype $s))\n!(get-state (repeat-state))\n!(get-state (repeat-state))\n!(let $s (repeat-write) (get-metatype $s))\n!(let $s (repeat-write) (get-metatype $s))\n!(get-state (repeat-write))\n!(get-state (repeat-write))\n', '[Grounded]\n[Grounded]\n[7]\n[7]\n[Grounded]\n[Grounded]\n[8]\n[8]\n', True),
    ('native-state-unused-operation-override', '(= (only-new-state) (new-state 7))\n!(add-atom &self (= (change-state! $s $value) supplied))\n!(get-state (only-new-state))\n!(get-state (only-new-state))\n', '[()]\n[7]\n[7]\n', True),
])

# Completed constructor provenance survives later program mutation; raw results do not.
CASES.extend([
    ('completed-constructor-survives-new-rule', '(= (make) (Box 1))\n!(let $value (make) (let $write (add-atom &self (= (Box 1) 9)) $value))\n!(+ 20 22)\n', '[(Box 1)]\n[42]\n', False),
    ('completed-constructor-after-child-reduction', '(= (make) (Box (+ 1 2)))\n!(let $value (make) (let $write (add-atom &self (= (Box 3) 9)) $value))\n', '[(Box 3)]\n', False),
    ('completed-constructor-call-remains-computation', '(= (Box $n) (+ $n 1))\n(= (make) (Box 1))\n!(make)\n', '[2]\n', False),
    ('raw-collection-return-still-computes', '(= (raw-group) (collapse (superpose (Box 1))))\n!(let $value (raw-group) (let $write (add-atom &self (= (Box 1) 9)) $value))\n', '[9]\n', True),
    ('raw-state-return-stays-syntax', '(= (stored-code) (new-state (+ 1 2)))\n!(get-state (stored-code))\n', '[(+ 1 2)]\n', True),
    ('mixed-completion-keeps-canonical-prefix', '(= (mixed $which) (let $write (add-atom &self (completed-prefix $which)) (case $which ((left (Box 1)) (right (collapse (superpose (Box 1))))))))\n!(let $value (mixed left) (let $write (add-atom &self (= (Box 1) 9)) $value))\n!(match &self (completed-prefix $which) $which)\n', '[(Box 1)]\n[left]\n', False),
    ('case-completed-constructor-survives-rule', '(= (choose-box $which) (case $which ((left (Box 1)) (right (Box 2)))))\n!(let $value (choose-box left) (let $write (add-atom &self (= (Box 1) 9)) $value))\n', '[(Box 1)]\n', True),
])

# Declared result demand changes whether the RHS is code or Atom data.
CASES.extend([
    ('atom-range-constructor-stays-raw', '(: make (-> Atom))\n(= (make) (Box 1))\n!(let $value (make) (let $write (add-atom &self (= (Box 1) 9)) $value))\n', '[9]\n', False),
    ('undefined-range-constructor-is-completed', '(: make (-> %Undefined%))\n(= (make) (Box 1))\n!(let $value (make) (let $write (add-atom &self (= (Box 1) 9)) $value))\n', '[(Box 1)]\n', False),
])
NATIVE_CONSTRUCTOR_BARRIERS = {
    'native-space-arity-barrier',
    'native-space-definition-barrier',
    'native-space-nr-barrier',
    'native-space-open-barrier',
    'native-space-type-barrier',
    'native-state-stays-canonical',
    'native-state-nullary-barrier',
    'native-state-extra-argument-barrier',
    'native-state-type-barrier',
    'native-state-fresh-variable-barrier',
    'native-state-payload-resource-barrier',
    'native-state-write-computed-value-barrier',
    'native-state-write-computed-reference-barrier',
    'native-state-write-nondeterminism-barrier',
    'native-state-write-arity-barrier',
    'native-state-readback-barrier',
}

# Complete host counts detect constructor/write contraction, duplication and
# replay. The error-prefix fixture also includes one native add-atom call;
# the syntax fixture includes an inert, unexecuted nested constructor.
NATIVE_STATE_HOST_CALLS = {
    'native-state-drop-loop': 32,
    'native-state-retained-alias': 34,
    'native-state-payload-stays-syntax': 3,
    'native-state-reread-completes-once': 1,
    'native-state-duplicates': 3,
    'native-state-definition-invalidation': 1,
    'native-state-signature-invalidation': 1,
    'native-state-write-overwrite-alias': 33,
    'native-state-write-old-read': 2,
    'native-state-write-type-error': 2,
    'native-state-write-reference-error': 1,
    'native-state-write-error-prefix': 3,
    'native-state-write-caller-old-image': 1,
    'native-state-write-definition-invalidation': 2,
    'native-state-write-native-type-authority': 4,
    'native-state-write-repeat-authority': 12,
    'native-state-unused-operation-override': 2,
}

for name, expected in (
        ("control_source_single_positive", "consumed-once\n[(Done 3)]\n"),
        ("control_source_branching_positive", "[(one one 7), (one one 7), (two two 8)]\n"),
        ("control_caller_context_negative", "[(Kept 9)]\n")):
    CASES.append((name, (ROOT / "tests/filesystem_v1" / (name + ".metta")).read_text(),
                  expected, False))

PROFILE_CASES = [
    ("native-space-profile-contract", "(= (profile-space) "
     "(let $drop (new-space) done))\n!(profile-space)\n!(+ 20 22)\n",
     "[done]\n[42]\n"),
    ("original-call-fallback-after-input-retirement", "(: sentinel (-> Number Number))\n"
     "(= (sentinel $x) NotReducible)\n!(sentinel 8)\n"
     "!(let $value (sentinel 8) (Observed $value))\n",
     "[(sentinel 8)]\n[(Observed (sentinel 8))]\n"),
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

HEAD_AUTHORITY_CASES = [
    ("last", "(= (last $items) 88)\n!(last (a b b))\n", "[88]\n"),
    ("reverse", "(= (reverse $items) 88)\n!(reverse (a b b))\n", "[88]\n"),
    ("member", "(= (member $x $items) 88)\n!(member a (a b))\n", "[88]\n"),
    ("min", "(= (min $a $b) 88)\n!(min 2 3)\n", "[88]\n"),
    ("max", "(= (max $a $b) 88)\n!(max 2 3)\n", "[88]\n"),
    ("unknown-last", "!(last (a b b))\n", "[(last (a b b))]\n"),
    ("nested-last", "(= (last $items) 88)\n"
     "(= (wrap $x) (Pair (last $x) (last $x)))\n!(wrap (a b b))\n",
     "[(Pair 88 88)]\n"),
    ("duplicate-last", "(= (last $items) 88)\n(= (last $items) 88)\n"
     "!(last (a b b))\n", "[88, 88]\n"),
    ("other-arity", "(= (last $x $y) 88)\n!(last a b)\n", "[88]\n"),
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

# CLI fuel is an observation allowance: exhaustion is explicit, not a
# successful empty answer. Completed observations keep their ordinary meaning.
# Sweep finite budgets instead of encoding the retired recursive-depth counts.
FUEL_CASES = ("duplicates", "minimal-return", "entered-empty", "source-demand", "state-effects")
FUEL_BUDGETS = (1, 3, 7, 16, 64)


def check_fuel_result(name, fuel, result, expected):
    if result.returncode == 0:
        if result.stdout != expected or result.stderr:
            raise AssertionError((name, fuel, "completed", result.stdout, result.stderr))
    elif result.returncode == 1 and result.stderr == "error: observation incomplete: fuel-exhausted\n":
        # Exhaustion cannot fabricate a completed answer. Previously executed
        # effects remain visible exactly once, in their authored order.
        allowed = ("", "one\n", "one\ntwo\n") if name == "state-effects" else ("",)
        if result.stdout not in allowed:
            raise AssertionError((name, fuel, "unpublished or duplicated output", result.stdout))
    else:
        raise AssertionError((name, fuel, result.returncode, result.stdout, result.stderr))
    if fuel == FUEL_BUDGETS[0] and result.returncode != 1:
        raise AssertionError((name, "insufficient fuel reported success"))
    if fuel == FUEL_BUDGETS[-1] and result.returncode != 0:
        raise AssertionError((name, "finite completion control did not complete"))


VARIABLE_OR_STRING = re.compile(r'"(?:\\.|[^"\\])*"|\$[^\s()\[\],"]+')


def source_variables(text):
    return {match.group(0) for match in VARIABLE_OR_STRING.finditer(text)
            if match.group(0).startswith('$')}


def private_names(text, protected=()):
    """Compare variable suffixes bijectively; all other text stays literal."""
    protected = set(protected)
    names, parts, start = {}, [], 0
    for match in VARIABLE_OR_STRING.finditer(text):
        token = match.group(0)
        if not token.startswith('$') or token in protected:
            continue
        scoped = re.fullmatch(r'(.*)#([0-9]+)', token)
        if scoped is None:
            continue
        parts.append(text[start:match.start()])
        identity = names.setdefault(scoped[2], len(names))
        parts.append(('private-variable', scoped[1], identity))
        start = match.end()
    parts.append(text[start:])
    return tuple(parts)


def check_private_observer():
    left = '($local#31 $local#31 $other#31 $caller "literal#17" symbol#19)'
    right = '($local#42 $local#42 $other#42 $caller "literal#17" symbol#19)'
    assert private_names(left) == private_names(right)
    for changed in [right.replace('$local#42 $local#42', '$local#42 $local#43'),
                    right.replace('$other', '$changed'),
                    right.replace('$caller', '$different'),
                    right.replace('literal#17', 'literal#18'),
                    right.replace('symbol#19', 'symbol#20')]:
        assert private_names(left) != private_names(changed)
    assert private_names('$caller#0', {'$caller#0'}) != private_names(
        '$caller#42', {'$caller#0'})
    assert private_names('($hole#31 $hole#32)') != private_names(
        '($hole#42 $hole#42)')
    faithful = '[(Pair ($hole#31 $hole#31) "($hole#31 $hole#31)")]\n'
    expected = '[($hole#0 $hole#0)]\n'
    check_reflected_text(faithful, expected, ())
    for changed in (faithful.replace('"($hole#31 $hole#31)"', '"($hole#0 $hole#0)"'),
                    faithful.replace('$hole#31 $hole#31', '$hole#31 $hole#32')):
        try:
            check_reflected_text(changed, expected, ())
        except AssertionError:
            continue
        raise AssertionError(("invalid readback accepted", changed))


def check_reflected_text(output, expected, protected):
    """This fixture returns a structured term beside its own formatted text.

    Compare names only in the structured observation. The string must be an
    exact rendering of that run's term; generated digits are not erased from
    ordinary string data or compared with another run's allocator sequence.
    """
    strings = [m for m in VARIABLE_OR_STRING.finditer(output)
               if m.group(0).startswith('"')]
    if not output.startswith('[(Pair ') or len(strings) != 1:
        raise AssertionError(("reflective return shape", output))
    text = strings[0]
    if output[text.end():] != ')]\n':
        raise AssertionError(("reflective return suffix", output))
    raw = output[len('[(Pair '):text.start()].rstrip()
    if json.loads(text.group(0)) != raw:
        raise AssertionError(("formatted text lost the chosen names", output))
    if private_names('[' + raw + ']\n', protected) != private_names(expected, protected):
        raise AssertionError(("reflected structure or aliases changed", output))


# These paths may complete on the prepared tier before allocating the shared
# machine. All worker selectors must retain the exact completed/raw answers.
COMPLETED_RETURN_CONTROLS = {
    'completed-constructor-survives-new-rule',
    'completed-constructor-after-child-reduction',
    'completed-constructor-call-remains-computation',
    'raw-collection-return-still-computes',
    'raw-state-return-stays-syntax',
}
HEAD_AUTHORITY_CASES.extend((name, source, expected)
    for name, source, expected, _ in CASES if name in COMPLETED_RETURN_CONTROLS)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--timeout", type=float, default=30,
                        help="Per-execution timeout in seconds; zero disables it (default: 30)")
    options = parser.parse_args()
    if not math.isfinite(options.timeout) or options.timeout < 0:
        parser.error("--timeout must be finite and nonnegative")
    binary = options.binary.resolve()
    timeout = options.timeout or None
    root = Path(__file__).resolve().parents[2]
    check_private_observer()
    observations = 0
    with tempfile.TemporaryDirectory(prefix="he-shared-", dir=root / "runtime") as directory:
        for name, source, expected, compiled in CASES:
            path = Path(directory) / (name + ".metta")
            path.write_text(source)
            protected = source_variables(source)
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
                                        text=True, timeout=timeout)
                if result.returncode or private_names(result.stdout, protected) != private_names(expected, protected):
                    raise AssertionError((name, canonical, enabled, result.returncode,
                                          result.stdout, result.stderr))
                if enabled != "0" and compiled and "PETTA_MACHINE_STATS" not in result.stderr:
                    raise AssertionError((name, "compiled route was not exercised"))
                if (name in NATIVE_CONSTRUCTOR_BARRIERS and any(
                        int(value) != 0 for value in re.findall(
                            r"goal_host=([0-9]+)", result.stderr))):
                    raise AssertionError((name, "native constructor barrier was bypassed",
                                          result.stderr))
                if (name.startswith("native-space-") and compiled and enabled != "0" and
                        not any(int(value) > 0 for value in re.findall(
                            r"goal_host=([0-9]+)", result.stderr))):
                    raise AssertionError((name, "native constructor host was not exercised",
                                          result.stderr))
                if name in NATIVE_STATE_HOST_CALLS:
                    calls = sum(map(int, re.findall(r"goal_host=([0-9]+)",
                                                   result.stderr)))
                    wanted = NATIVE_STATE_HOST_CALLS[name] if enabled != "0" else 0
                    if calls != wanted:
                        raise AssertionError((name, "native State host count", calls,
                                              wanted, canonical, enabled, result.stderr))
                if (name == "native-space-reread-completes-once" and enabled != "0" and
                        sum(map(int, re.findall(r"goal_host=([0-9]+)",
                                                result.stderr))) != 1):
                    raise AssertionError((name, "constructor replayed across collected readback",
                                          result.stderr))
                if name == "pattern-callback-effect" and any(
                    int(value) != 0 for value in re.findall(
                        r"(?:goal_host|equation_match_attempts)=([0-9]+)", result.stderr)):
                    raise AssertionError((name, "pattern callback bypassed admission"))
                observations += 1
        # The shared tier itself must publish constructor provenance, even
        # when both prepared tiers have been explicitly bypassed.
        for name, source, expected, _ in CASES:
            if name not in COMPLETED_RETURN_CONTROLS:
                continue
            path = Path(directory) / ("forced-shared-" + name + ".metta")
            path.write_text(source)
            for canonical in ("0", "1"):
                env = dict(os.environ, CETTA_HE_SHARED_EXECUTION="1",
                           CETTA_HE_CANONICAL_EXECUTION=canonical,
                           CETTA_PREPARED_PURE_REFERENCE="1",
                           CETTA_OPEN_EQUATIONS_REFERENCE="1",
                           CETTA_PETTA_MACHINE_STATS="1")
                result = subprocess.run([str(binary), "--lang", "he", str(path)],
                                        cwd=root, env=env, capture_output=True,
                                        text=True, timeout=30)
                if (result.returncode or result.stdout != expected or
                        "PETTA_MACHINE_STATS" not in result.stderr):
                    raise AssertionError((name, "forced shared return provenance",
                                          canonical, result.returncode,
                                          result.stdout, result.stderr))
                observations += 1
        # A Work owns source admission across cache eviction and suspension.
        # Its native State semantics must not depend on a TLS authority slot
        # populated by creation still being warm at the next observation.
        work_cases = (
            ("work-native-state-cold-authority", """(: seed Atom)
(= (fresh-work-state) (new-state seed))
(= (pure-noop $n) (if (< $n 1) 42 (+ $n 1)))
!(let $w (work-new (fresh-work-state))
  (let $ignored (pure-noop 0)
    (let (taken ($s) $status) (work-take $w 1)
      (Pair (get-metatype $s) (get-state $s)))))
""", "[(Pair Grounded seed)]\n", 1),
            ("work-native-state-resume-no-replay", """(= (state-answers)
  (let $s (new-state 0)
    (let $write (change-state! $s 7) (superpose ($s $s)))))
(= (pure-noop $n) (if (< $n 1) 42 (+ $n 1)))
!(let $w (work-new (state-answers))
  (let (taken ($first) $first-status) (work-take $w 1)
    (let $old (get-state $first)
      (let $external-write (change-state! $first 9)
        (let $ignored (pure-noop 0)
          (let (taken ($second) $second-status) (work-take $w 1)
            (Pair (Pair (get-metatype $first) $old)
                  (Pair (get-metatype $second) (get-state $second)))))))))
""", "[(Pair (Pair Grounded 7) (Pair Grounded 9))]\n", 2),
            ("work-native-state-budget-resume", """(= (finish-work-state $n $s)
  (if (== $n 0) (change-state! $s 7)
    (finish-work-state (- $n 1) $s)))
(= (start-work-state) (let $s (new-state 0) (finish-work-state 64 $s)))
(= (pure-noop $n) (if (< $n 1) 42 (+ $n 1)))
!(let $w (work-new (start-work-state))
  (let $budget (work-budget $w 32)
    (let $paused (work-take $w 1)
      (let $ignored (pure-noop 0)
        (let $unbounded (work-budget $w 0)
          (let (taken ($s) $status) (work-take $w 1)
            (Pair $paused (Pair (get-metatype $s) (get-state $s)))))))))
""", "[(Pair (taken () paused) (Pair Grounded 7))]\n", 2),
        )
        for name, source, expected, host_calls in work_cases:
            path = Path(directory) / (name + ".metta")
            path.write_text(source)
            for canonical, shared in (("0", "0"), ("0", "1"),
                                      ("1", "0"), ("1", "1")):
                # A pending canonical Work deliberately has no transition
                # budget. This pause oracle exercises the admitted machine.
                if name == "work-native-state-budget-resume" and shared == "0":
                    continue
                env = dict(os.environ, CETTA_HE_SHARED_EXECUTION=shared,
                           CETTA_HE_CANONICAL_EXECUTION=canonical,
                           CETTA_PREPARED_PURE_REFERENCE="1",
                           CETTA_OPEN_EQUATIONS_REFERENCE="1",
                           CETTA_PETTA_MACHINE_STATS="1")
                result = subprocess.run([str(binary), "--lang", "he",
                                         "--profile", "extended", str(path)],
                                        cwd=root, env=env, capture_output=True,
                                        text=True, timeout=30)
                calls = sum(map(int, re.findall(r"goal_host=([0-9]+)",
                                               result.stderr)))
                # Work does not publish its private machine counters. The
                # external mutation distinguishes an actual retained suffix
                # from replay: rebuilding or rewriting it would return 7.
                if result.returncode or result.stdout != expected:
                    raise AssertionError((name, "Work native authority/replay",
                                          canonical, shared, result.returncode,
                                          result.stdout, calls, result.stderr))
                observations += 1
        # The raw minimal expansion has private names. Return it as completed
        # data beside its formatting, so neither consumer interprets the
        # expansion merely to compare it. Preparation before the observation
        # may choose a different fresh-name schedule.
        expected = next(row[2] for row in CASES
                        if row[0] == 'raw-expansion-after-failure')
        body = '''!(let $raw (eval (_minimal-map-atom (1 2) $x (return $x)))
          (let $args (cons-atom $raw ())
            (let $rendered (format-args "{}" $args)
              (function (return (Pair $raw $rendered))))))\n'''
        prefixes = ('', '(= (miss $p) (let (Pair $x $y) $p 7))\n!(miss (Single 3))\n')
        for prefix in prefixes:
            source = prefix + body
            path = Path(directory) / "fresh-readback.metta"
            path.write_text(source)
            for canonical, shared in (("0", "0"), ("0", "1"),
                                      ("1", "0"), ("1", "1"), (None, None)):
                env = dict(os.environ)
                for key, value in (("CETTA_HE_CANONICAL_EXECUTION", canonical),
                                   ("CETTA_HE_SHARED_EXECUTION", shared)):
                    if value is None:
                        env.pop(key, None)
                    else:
                        env[key] = value
                result = subprocess.run([str(binary), "--lang", "he", str(path)],
                                        cwd=root, env=env, capture_output=True,
                                        text=True, timeout=30)
                if result.returncode or result.stderr:
                    raise AssertionError(("fresh readback", result.returncode, result.stderr))
                check_reflected_text(result.stdout, expected, source_variables(source))
                observations += 1
        # A backend's intrinsic name does not override an authored HE head.
        # All three worker switches are independent of source authority.
        for name, source, expected in HEAD_AUTHORITY_CASES:
            path = Path(directory) / ("head-authority-" + name + ".metta")
            path.write_text(source)
            for shared in ("0", "1"):
                for prepared in ("0", "1"):
                    for opened in ("0", "1"):
                        env = dict(os.environ, CETTA_HE_SHARED_EXECUTION=shared,
                                   CETTA_PREPARED_PURE_REFERENCE=prepared,
                                   CETTA_OPEN_EQUATIONS_REFERENCE=opened)
                        result = subprocess.run(
                            [str(binary), "--lang", "he", str(path)], cwd=root,
                            env=env, capture_output=True, text=True, timeout=30)
                        if (result.returncode, result.stdout, result.stderr) != (0, expected, ""):
                            raise AssertionError((name, shared, prepared, opened,
                                result.returncode, result.stdout, result.stderr))
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
                                        text=True, timeout=timeout)
                if result.returncode or result.stdout != expected or result.stderr:
                    raise AssertionError((name, canonical, enabled, result.returncode,
                                          result.stdout, result.stderr))
                observations += 1
        for name in FUEL_CASES:
            path = Path(directory) / (name + ".metta")
            expected = next(row[2] for row in CASES if row[0] == name)
            for enabled in ("0", "1"):
                env = dict(os.environ, CETTA_HE_CANONICAL_EXECUTION="1",
                           CETTA_HE_SHARED_EXECUTION=enabled,
                           CETTA_OPEN_EQUATIONS_REFERENCE="1",
                           CETTA_PREPARED_PURE_REFERENCE="1")
                completed = False
                for fuel in FUEL_BUDGETS:
                    result = subprocess.run([str(binary), "--lang", "he", "--fuel",
                                             str(fuel), str(path)], cwd=root, env=env,
                                            capture_output=True, text=True, timeout=timeout)
                    check_fuel_result(name, fuel, result, expected)
                    if completed and result.returncode:
                        raise AssertionError((name, enabled, fuel, "completion regressed with more fuel"))
                    completed |= result.returncode == 0
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
                                text=True, timeout=timeout)
        if result.returncode or result.stdout != "[done]\n" or result.stderr:
            raise AssertionError(("deep instructions", result.returncode,
                                  result.stdout, result.stderr))
        observations += 1
        # Plain values stay on the dialect's value/cast boundary; interpreting
        # them must not allocate a relational computation episode. A call
        # whose result is a plain value is one episode.
        env = dict(os.environ, CETTA_HE_CANONICAL_EXECUTION="1",
                   CETTA_HE_SHARED_EXECUTION="1", CETTA_PETTA_MACHINE_STATS="1",
                   CETTA_PREPARED_PURE_REFERENCE="1", CETTA_OPEN_EQUATIONS_REFERENCE="1")
        for name, source, expected, episodes in (
                ("plain-values", '! 16\n! spelling\n! $caller\n! True\n',
                 "[16]\n[spelling]\n[$caller]\n[True]\n", 0),
                ("plain-call", '(= (plain $n) (if (< $n 10) 16 17))\n!(plain 3)\n',
                 "[16]\n", 1)):
            path = Path(directory) / (name + ".metta")
            path.write_text(source)
            result = subprocess.run([str(binary), "--lang", "he", str(path)],
                                    cwd=root, env=env, capture_output=True,
                                    text=True, timeout=timeout)
            lines = result.stderr.splitlines()
            if (result.returncode or result.stdout != expected or
                    len(lines) != episodes or
                    any(not line.startswith("PETTA_MACHINE_STATS ") for line in lines)):
                raise AssertionError((name, result.returncode,
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
                                text=True, timeout=timeout)
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
                                    text=True, timeout=timeout)
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
                                    text=True, timeout=timeout)
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
