; Symbols the program defines are produced once per branch through a shared
; cell, by need, on every route: the default route and the canonical
; evaluator agree on sharing, on cycles and on demanded prefixes.
(= coin heads)
(= coin tails)
(= (pairc $x) (P $x $x))
!(P coin coin)
!(pairc coin)
(= ones (Cons 1 ones))
(= (head (Cons $h $t)) $h)
(= (tail (Cons $h $t)) $t)
(= (take 0 $s) Nil)
(= (take $n (Cons $h $t)) (if (> $n 0) (Cons $h (take (- $n 1) $t)) (empty)))
!(head (tail (tail ones)))
!(take 3 ones)
! ones
(= bits (Cons 0 bits))
(= bits (Cons 1 bits))
!(take 2 bits)
(= a b)
(= b a)
(= (k $x $y) $x)
!(k 1 a)
! a
