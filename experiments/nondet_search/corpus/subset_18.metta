(= (ss Nil $t) (if (== $t 0) 1 (empty)))
(= (ss (Cons $h $r) $t) (ss $r $t))
(= (ss (Cons $h $r) $t) (if (>= $t $h) (ss $r (- $t $h)) (empty)))
!(size-atom (collapse (ss (Cons 1 (Cons 8 (Cons 4 (Cons 11 (Cons 7 (Cons 3 (Cons 10 (Cons 6 (Cons 2 (Cons 9 (Cons 5 (Cons 1 (Cons 8 (Cons 4 (Cons 11 (Cons 7 (Cons 3 (Cons 10 Nil)))))))))))))))))) 40)))
