; HE-backed unify branch semantics under the surrounding evaluator:
; primitive unify chooses a branch, and the enclosing evaluation context
; continues from that chosen branch.
!(unify A A (+ 1 2) 0)
!(unify A B (+ 1 2) 0)
!(unify ($h $t) (1 (2 3)) (cons-atom $h $t) nope)
