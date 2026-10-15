; Restore a captured invocation at valid hash-table occupancy above 50%.
; Normal insertion grows at 70%; restoring cannot impose a tighter rule.
(= (memo:step K0 $n) (- $n 1))
(= (memo:step K1 $n) (- $n 1))
(= (memo:loop $n)
   (if (== $n 0) done (memo:loop (memo:step K1 $n))))
!(memo:loop 10)
!(memo:loop 8)
!(memo:loop 11)
