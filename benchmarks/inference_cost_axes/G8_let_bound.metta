; Guard argument is a let-bound variable (skeleton plus environment).
; Resolving through the live environment must keep `hit`; treating $n as
; unbound must still not drop it.
(= (g8 $s) (if (< 2 $s) hit (empty)))
!(let $n 5 (g8 $n))
