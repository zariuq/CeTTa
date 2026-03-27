; HE-backed grounded op: intersection-atom keeps left-side order and uses
; multiset semantics, consuming one matching RHS occurrence per output.

!(assertEqual (intersection-atom (a b c c) (b c c c d)) (b c c))
!(assertEqual (intersection-atom ((x y) z (x y) a) ((x y) b (x y) (x y))) ((x y) (x y)))
!(assertEqual (intersection-atom () (x y)) ())
