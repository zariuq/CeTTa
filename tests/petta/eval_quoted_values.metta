; eval translates its argument at run time, as PeTTa's eval does: a quoted
; argument is left as the value it is, unread, so a value that shares its
; subterms costs nothing to quote, however large it is unshared (SWI-PeTTa's
; answers).

(= (dag $n $x) (if (== $n 0) $x (let $t (dag (- $n 1) $x) (Node $t $t))))
(= (f $x) (+ $x 1))
(= (ev $x) (eval $x))

!(eval (quote (+ 1 2)))
!(eval (+ 1 2))
!(let $q (quote (f 1)) (eval $q))
!(eval (foo (quote (+ 1 2)) (+ 1 2)))
!(ev (quote (+ 1 2)))
!(ev (f 2))
!(msort (collapse (let $x (superpose (1 2 3)) (eval (quote (g $x))))))
!(msort (collapse (let $x (superpose (1 2 3)) (eval (g (f $x))))))
!(let $d (dag 12 Leaf) (size-atom (eval (quote $d))))
!(let $d (dag 40 Leaf) (size-atom (eval (quote $d))))
!(let $d (dag 40 Leaf) (size-atom (eval (Pair (quote $d) (f 1)))))
!(let $d (dag 40 Leaf) (let (Node $l $r) (eval (quote $d)) (== $l $r)))
