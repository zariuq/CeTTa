; No profile declaration: local binding is the default for written and built code.
(= (twice $f) (Pair ($f 1) ($f 2)))
!(twice (lam z (let $y z (g $y))))
!(twice (cons-atom lam (z (let $y z (g $y)))))
!(let $y 7 (let $y $y $y))
!(let $y 7 (new ($y) (let $y $y $y)))
; Crossing explicitly requests refinement of the outer slot.
!(collapse (let $y 7 (let $y 8 $y){$y}))
!(let $y 7 (Pair (let $y 8 $y) $y))
