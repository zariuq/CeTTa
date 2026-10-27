; A closure passed beside a long data argument is specialized at its
; relation's first call: the bounded relevance scan finds the closure before
; it reaches the end of the data.
(= (add-k $k $x) (+ $k $x))
(= (walk $f (One $x)) ($f $x))
(= (walk $f (Many $x $xs)) (+ ($f $x) (walk $f $xs)))
(= (build-chain $n) (if (== $n 0) (One 0) (Many $n (build-chain (- $n 1)))))
!(walk (add-k 1) (build-chain 40))
!(walk (+ 2) (build-chain 40))
(= (cells $f (One $x)) (cons ($f $x) ()))
(= (cells $f (Many $x $xs)) (cons ($f $x) (cells $f $xs)))
!(cells (add-k 10) (build-chain 40))
