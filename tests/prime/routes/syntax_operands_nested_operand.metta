; The same operators inside an operand that another form evaluates: the
; scrutinee of a case or a switch, the source of a chain, the argument of a
; collapse, a field of a tuple.  Such a form evaluates its operand on its
; own and joins the caller's bindings to each answer; the names those
; bindings give values are read through them first, so the operators see
; the caller's values.  This file is run with and without --fuel; the
; answers must be the same on both routes, and they are the reference HE's.

(= (h $x) (unify $x 5 ok no))
(fact 5)
(fact 6)

; ---- After `(h $y)` the environment binds $y to 5.
!(let $r (h $y) (case (unify $y 6 yes no) ((yes Y) (no N))))
!(let $r (h $y) (switch (unify $y 6 yes no) ((yes Y) (no N))))
!(let $r (h $y) (chain (unify $y 6 yes no) $z $z))
!(let $r (h $y) (collapse (unify $y 6 yes no)))
!(let $r (h $y) (collapse (switch $y ((6 six) (5 five)))))
!(let $r (h $y) (case (switch $y ((6 six) (5 five))) (($v (got $v)))))
!(let $r (h $y) (collapse (match &self (fact $y) $y)))
!(let $r (h $y) (Pair (unify $y 6 yes no) x))
!(let $r (h $y) (Pair $y (unify $y 6 yes no)))
!(let $r (h $y) (row (match &self (fact $y) $y) (switch $y ((5 five)))))

; ---- A binding that agrees still matches; a name the environment does
; not bind still binds.
!(let $r (h $y) (case (unify $y 5 yes no) ((yes Y) (no N))))
!(let $r (h $y) (collapse (unify $z 6 (yes $z) no)))
!(let $r (h $y) (Pair (unify $y 5 yes no) (unify $z 6 (got $z) no)))
!(let $r (h $y) (collapse (match &self (fact $z) $z)))
