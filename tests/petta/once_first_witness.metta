; `once` answers some witness of its body.  A body whose depth-first order
; would first enter a branch that never returns still has its witness.
(= (loop) (loop))
(= (loop) found)
!(once (loop))

; A finite search without a witness answers nothing.
(= (fin z) (empty))
(= (fin (s $x)) (fin $x))
!(once (fin (s (s z))))

; A body with an effect keeps depth-first order.
(= (noisy) (progn (println! first) one))
(= (noisy) (progn (println! second) two))
!(once (noisy))

; A cheap depth-first witness is the one returned.
(= (pick) a)
(= (pick) b)
!(once (pick))
