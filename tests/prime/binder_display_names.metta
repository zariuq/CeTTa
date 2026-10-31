; A binder renamed to avoid capture shows its own name with the least
; numeric suffix that nothing in its scope spells.  The printed term reads
; back as the same term.
!((lam a (lam x (lam y (pair a (pair x y))))) (pair x y))
!((((lam a (lam x (lam y (pair a (pair x y))))) (pair x y)) 1) 2)
!(((lam $x1 (lam $y1 (pair (pair x y) (pair $x1 $y1)))) 1) 2)

; Substitution never captures a name of the value.
!(((lam a (lam x (pair a x))) x) 8)
!(let $v (pair x y) (lam x (lam y (pair $v (pair x y)))))
!(((let $v (pair x y) (lam x (lam y (pair $v (pair x y))))) 1) 2)

; A suffix that the value already spells is skipped.
!((lam a (lam x (pair a (pair x $x1)))) (pair x $x1))
!((lam a (lam x (pair a (pair x x1)))) (pair x x1))

; A binder that captures nothing keeps its name.
!((lam a (lam x (pair a x))) q)
