; A value that shares each level twice: its tree is exponential in its depth
; while the value is linear.  Reading it back through an equation's right
; side substitutes each shared node once, on every route, including the one
; a machine transition bound selects.
(= (dag $n) (if (== $n 0) (leaf $z) (let $t (dag (- $n 1)) (node $t $t $z))))
(= (depth (leaf $x)) 0)
(= (depth (node $l $r $v)) (+ 1 (depth $l)))
!(let $r (dag 60) (depth $r))
