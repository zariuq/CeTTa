; Expected (0 1): the then branch calls a two-clause function.
; SWI-PeTTa 6586540 answers (1) when the else branch holds a let.
(= (k $a $b) 0)
(= (k $a $b) $b)
(= (h $n) (if (== $n $n) (k 4 $n) (let $x 4 $n)))
!(collapse (h 1))
