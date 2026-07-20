; Canonical Prime ABT application is non-strict and shares each demanded
; argument within a branch.  Ordinary equation application is covered by the
; companion conformance gate.

!(App (Lam T (Var 0)) (superpose (A B)))
!(App (Lam T (Pair (Var 0) (Var 0))) (superpose (A B)))
!(App (Lam T kept) (== 1 "bad"))

(= (need-ignore $x) kept-ordinary)
(= (need-dup $x) (Pair $x $x))
!(need-ignore (== 1 "bad"))
!(need-dup (superpose (A B)))

; Each equation is its own Need branch.  The structural clause demands only
; its first argument; the wildcard clause and both unused second arguments do
; not force their thunks.
(= (need-classify A $unused) alpha)
(= (need-classify $x $unused) other)
!(need-classify (superpose (A B)) (== 1 "unused"))

; A nonlinear LHS must compare values, not the distinct identities of two
; thunks.  The second, linear clause stays lazy.
(= (need-same $x $x) same)
(= (need-same $x $y) any)
!(need-same (superpose (A B)) A)

; Nested structure demands the enclosing argument but leaves the unrelated
; argument suspended.
(= (need-box (Box A) $unused) boxed-a)
!(need-box (Box (superpose (A B))) (empty))
