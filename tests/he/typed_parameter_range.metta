; A range written as one parameter's type variable: HE evaluates each answer
; again at that argument's type.  The prepared machine checks each answer
; against the argument's class, and leaves any other argument or answer to
; the canonical evaluator.
(: cnt (-> Number $t $t))
(= (cnt $n $acc) (if (== $n 0) $acc (cnt (- $n 1) (+ $acc 1))))
!(cnt 100 0)
(: keep (-> Number $t $t))
(= (keep $n $s) (if (== $n 0) $s (keep (- $n 1) $s)))
!(keep 3 "s")
!(keep 2 True)
!(keep 2 foo)
(: lead (-> $t Number $t))
(= (lead $x $n) (if (== $n 0) $x (lead $x (- $n 1))))
!(lead 7 3)
!(lead "w" 2)
(: grow (-> Number $t $t))
(= (grow $n $acc) (if (== $n 0) (+ $acc 0.5) (grow (- $n 1) $acc)))
!(grow 2 1)
(: dbl (-> Number Number))
(= (dbl $x) (* 2 (keep 1 $x)))
!(dbl 4)
; An answer outside the argument's class is HE's type error.
(: bad (-> Number $t $t))
(= (bad $n $acc) (if (== $n 0) "s" (bad (- $n 1) $acc)))
!(collapse (bad 2 0))
; Each alternative is checked.
(: pair (-> Number $t $t))
(= (pair $n $acc) (if (== $n 0) $acc (superpose ((pair (- $n 1) $acc) (pair (- $n 1) (+ $acc 1))))))
!(collapse (pair 2 0))
; A symbol's declared type fixes the variable at that type.
(: foo Foo)
!(keep 2 foo)
; A symbol declared at a metatype's name can fix it at Atom, whose answer HE
; keeps as data: every call is then the canonical one.
(: qa Atom)
!(keep 2 qa)
!(keep 2 5)
