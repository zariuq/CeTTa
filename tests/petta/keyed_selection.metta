; Selecting an equation by the keys its patterns test.  Every answer is the
; one scanning every equation gives.

; One position: the first argument's key.
(= (app Nil $ys) $ys)
(= (app (Cons $x $xs) $ys) (Cons $x (app $xs $ys)))
!(app (Cons 1 (Cons 2 Nil)) (Cons 3 Nil))

; Two positions: the first key is shared, the second tells them apart.
(= (pack Z $n) (S $n))
(= (pack (S $m) Z) (pack $m (S Z)))
(= (pack (S $m) (S $n)) (pack $m (pack (S $m) $n)))
!(pack (S (S Z)) (S Z))

; A variable at a tested position admits any argument there.
(= (f a $y) (one $y))
(= (f b c) two)
(= (f b d) three)
!(f a zz)
!(f a (deep x))
!(f b c)
!(f b d)
!(f b e)

; Arguments without a key at a tested position: a number, and a list built
; by cons, which spells the expression it lists.
!(f 5 c)
(= (pk Z) zero)
(= (pk (S $m)) (succ $m))
!(pk (cons-atom S (cons-atom Z ())))
!(pk (S Z))

; Patterns beyond the key: a nested constructor, and a repeated variable.
(= (h (P (S $x))) (inner $x))
(= (h (Q $y)) (other $y))
!(h (P (S k)))
!(h (P Z))
!(h (Q k))
(= (r (K $x $x)) same)
(= (r (L $x)) (other $x))
!(r (K a a))
!(r (K a b))
!(r (L a))

; A body that builds around its call, over two keyed positions.
(= (zip Nil $ys) Nil)
(= (zip (Cons $x $xs) Nil) Nil)
(= (zip (Cons $x $xs) (Cons $y $ys)) (Cons (P $x $y) (zip $xs $ys)))
!(zip (Cons 1 (Cons 2 (Cons 3 Nil))) (Cons a (Cons b Nil)))

; Two equations no position tells apart are not keyed: both answer.
(= (g $x) (any $x))
(= (g a) only-a)
!(size-atom (collapse (g a)))
