; A stored value's frame identifiers are found in the value's own size.
; Each opens level adds a variable of a fresh activation, so the list at depth
; k names k frames through one source variable.  The dag shares each level
; twice and adds a variable at every level, so its tree is exponential in its
; depth while the value itself is linear.
(= (opens $n) (if (== $n 0) () (cons (pair $n $x) (opens (- $n 1)))))
(= (dag $n) (if (== $n 0) (leaf $z) (let $t (dag (- $n 1)) (node $t $t $z))))
(= (depth (leaf $x)) 0)
(= (depth (node $l $r $v)) (+ 1 (depth $l)))
!(length (opens 2000))
!(let $r (dag 40) (depth $r))
