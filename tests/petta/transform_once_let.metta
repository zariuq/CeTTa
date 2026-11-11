; The program's first transform is the value of a top-level let, under once:
; its count, and then its caller's instance, reach the let's body.
!(bind! &l (new-space))
!(add-atom &l (job a))
!(let $n (once (transform &l (take (job $j)) (done $j) &l)) (let-count $n))
!(add-atom &l (job b))
!(let $v (once (transform &l (take (job $j)) (kept $j))) (let-value $v))
!(let-remaining (sort-atom (collapse (match &l $a $a))))
