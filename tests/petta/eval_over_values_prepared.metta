; eval runs the term written in it once.  Where its variables hold values,
; as a fold step's binders and a body's parameters do, a number, a symbol or
; a string is data where eval reads it, and an expression is run as code.
(= (f $x) (* $x 10))
(= (inc $x) (+ $x 1))
(= (dbl $x) (* $x 2))
(= (twice $x) (eval (+ $x $x)))
!(foldl-atom (1 2 3) 0 $acc $item (eval (+ $acc $item)))
!(foldl-atom (same other same) 0 $acc $item (eval (if (== $item same) (+ $acc 1) $acc)))
!(twice 4)
; Expression values: eval runs each one.
!(let $items (cons-atom (cons-atom f (1)) (cons-atom (cons-atom f (2)) ())) (foldl-atom $items 0 $acc $item (eval (+ $acc $item))))
!(let $e (cons-atom f (2)) (twice $e))
; A variable operator names the code eval runs.
!(let $ops (cons-atom inc (cons-atom dbl ())) (foldl-atom $ops 1 $acc $op (eval ($op $acc))))
