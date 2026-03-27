; HE-backed grounded op: subtraction-atom removes one matching RHS occurrence
; at a time, preserving the remaining LHS order.

!(assertEqual (subtraction-atom (a b b c) (b c c d)) (a b))
!(assertEqual (subtraction-atom ((x y) z (x y) a) ((x y) (x y) b)) (z a))
!(assertEqual (subtraction-atom () (x y)) ())
