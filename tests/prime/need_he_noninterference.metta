; This fixture pins the four existing HE profiles while Prime acquires its own
; application strategy.  The profiles intentionally do not agree with each
; other; the invariant is that this tranche changes none of them.
(= (need-ni-ignore $x) kept)
(= (need-ni-dup $x) (Pair $x $x))
!(need-ni-dup (superpose (A B)))
!(need-ni-ignore (empty))
!(need-ni-ignore (== 1 "bad"))
