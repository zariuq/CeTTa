; Patterns holding a variable where a relation of deep facts has a symbol
; heading an expression of its spine: the variable meets the symbol, so the
; facts the relation keeps answer beside the rows.  With relations from a
; head's first fact, the first word fixes the spine (w (a _ _)); the words
; of other first letters stay rows.
!(bind! &w (new-space set))
(= (word $k) (superpose ((a b c) (a b d) (a c c) (b b c) (a b c) (c c c) (a d d) (b d c))))
!(size-atom (collapse (let $x (word 0) (add-atom &w (w $x)))))
!(sort-atom (collapse (match &w (w $x) $x)))
!(sort-atom (collapse (match &w (w ($c0 $c1 $c2)) ($c0 $c1 $c2))))
!(sort-atom (collapse (match &w (w (a $c1 $c2)) ($c1 $c2))))
!(sort-atom (collapse (match &w (w ($c0 b $c2)) ($c0 $c2))))
!(size-atom (collapse (match &w (w ($h $h $c)) t)))
!(size-atom (collapse (match &w (w ($h $c $c)) t)))
!(sort-atom (collapse (match &w (w ($h $c $c)) $h)))
; joined with itself on a column under the head
!(size-atom (collapse (match &w (w ($a b $c)) (match &w (w ($c $d $e)) t))))
