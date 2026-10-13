; A value computed while one of its variables is unbound is computed again
; once a later binding binds that variable: the body of a let is evaluated
; with its bindings applied, and let* is nested lets.

; Inside called functions, on every evaluation path.
(= (later-star $n) (let* (($a (+ $b 1)) ($b $n)) $a))
!(later-star 1)
(= (later-let $n) (let $a (+ $b 1) (let $b $n $a)))
!(later-let 1)
!(let $a (+ $b 1) (let $b 1 $a))
!(let* (($a (+ $b 1)) ($b 1)) $a)
(= (wrapped $n) (let $a (+ $b 1) (let $b $n (w $a))))
!(wrapped 1)
(= (f $x) (g $x))
(= (through-call $n) (let $a (f (+ $b 1)) (let $b $n $a)))
!(through-call 1)
(= (two-later $n) (let $a (+ $b $c) (let $b $n (let $c 2 $a))))
!(two-later 1)
(= (destructured $n) (let ($a $b) ((+ $c 1) $n) (let $c $b $a)))
!(destructured 1)
(= (in-argument $n) (let $a (+ $b 1) (let $b $n (+ $a 10))))
!(in-argument 1)
(= (in-tuple $n) (let $a (+ $b 1) (let $b $n (pair $a (w $a)))))
!(in-tuple 1)
(= (by-case $n) (let $a (+ $b 1) (case $n (($b $a)))))
!(by-case 1)
(= (in-condition $n) (let $a (+ $b 1) (let $b $n (if (== $a 2) yes no))))
!(in-condition 1)
(= (h $x) (* $x 3))
(= (in-call $n) (let $a (+ $b 1) (let $b $n (h $a))))
!(in-call 1)
(= (collected $n) (let $a (+ $b 1) (let $b $n (collapse $a))))
!(collected 1)
(= (partial $n) (let $a (pair $c (+ $b 1)) (let $b $n (let $c x $a))))
!(partial 1)
(val 1)
(= (by-match) (let $a (+ $b 1) (match &self (val $b) $a)))
!(by-match)

; A function result left stuck on a fresh variable, bound by a later pattern.
(= (stuck) (+ $q 1))
(= (by-pattern) (let $a (stuck) (let (+ 5 1) $a $a)))
!(by-pattern)

; A stored row with variables gives a stuck call with a fresh variable, which a
; later pattern binds.
(row (pair $v (+ $v 1)))
(= (getrow) (match &self (row $x) $x))
(= (use-row) (let (pair 5 $s) (getrow) $s))
!(use-row)
(= (use-row-case) (case (getrow) (((pair 7 $s) $s))))
!(use-row-case)

; Several values for the later binding, a quoted value, a variable that stays
; unbound, and the error of a bad later binding.
(= (several $n) (let $a (+ $b 1) (let $b (superpose (1 2)) $a)))
!(several 0)
(= (quoted $n) (let $a (quote (+ $b 1)) (let $b $n $a)))
!(quoted 1)
(= (never-bound $n) (let $a (+ $b 1) (let $c $n $a)))
!(let ($op $v $one) (never-bound 1) (quote ($op $one)))
!(collapse (let $a (+ $b 1) (let $b "s" $a)))
!(collapse (let* (($a (+ $b 1)) ($b "s")) $a))

; The program changes between the binding and the use: the stuck value is
; evaluated against the changed program.
(= (after-change) (let $a (foo) (let $u (add-atom &self (= (foo) 3)) $a)))
!(after-change)
(= (after-change-in-tuple) (let $a (bar) (let $u (add-atom &self (= (bar) 4)) (w $a))))
!(after-change-in-tuple)
