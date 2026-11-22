; Distinct rows are distinct declarations: a call that several of them admit
; runs once for its tuple of argument values, through a variable too.
(: g2 (-> Number Number))
(: g2 (-> $t $t))
(= (g2 $x) (+ $x 1))
(= (app $g $y) ($g $y))
!(collapse (app g2 1))
!(collapse (g2 1))
!(sort-atom (collapse (get-type g2)))
(= (loop $n $f $acc) (if (== $n 0) $acc (loop (- $n 1) $f ($f $acc))))
!(loop 100 (|-> ($x) (+ $x 1)) 0)
