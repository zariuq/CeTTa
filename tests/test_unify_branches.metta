; HE-backed unify branch semantics: the selected branch is evaluated, not
; returned as raw syntax.
!(unify A A (+ 1 2) 0)
!(unify A B (+ 1 2) 0)
!(unify ($h $t) (1 (2 3)) (cons-atom $h $t) nope)
