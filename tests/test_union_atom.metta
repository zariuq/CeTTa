; HE/PeTTa compatibility: union-atom appends tuple contents, preserving duplicates

!(assertEqual (union-atom (a b b c) (b c c d)) (a b b c b c c d))
!(assertEqual (union-atom () (x y)) (x y))
!(assertEqual (union-atom (Z (S Z)) ()) (Z (S Z)))
