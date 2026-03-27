; HE-backed grounded op: remove later duplicates while preserving first
; occurrence order.
!(assertEqual (unique-atom (a b a c b)) (a b c))
!(assertEqual (unique-atom ((x y) (x y) z (x y))) ((x y) z))
!(assertEqual (unique-atom (1 2 1 3 2)) (1 2 3))
