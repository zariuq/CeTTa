; Advanced unify operations

; Deeply nested unification
!(assertEqual (unify (f (g (h a))) (f (g (h $x))) $x no-match) a)

; Unify with expression result
!(assertEqual (unify (A B) ($x $y) (result $x $y) fail) (result A B))

; Unify fails on structure mismatch
!(assertEqual (unify (A B) (A B C) yes no) no)

; Unify with same variable used twice
!(assertEqual (unify (A A) ($x $x) (same $x) (diff)) (same A))

; Unify fails when same var would bind differently
!(assertEqual (unify (A B) ($x $x) (same $x) (diff)) (diff))
