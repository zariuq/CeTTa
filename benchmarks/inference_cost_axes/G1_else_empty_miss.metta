; Admission: else is authored empty.  Ground budget 1 selects empty.
; A second equation still answers, so a false prune of every g1-head
; would drop `survived`.
(= (g1 $s) (if (< 2 $s) hit (empty)))
(= (g1 $s) survived)
!(g1 1)
