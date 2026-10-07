; A symbol's cell is found again in time independent of how long the branch's
; history has grown: a loop of sixty thousand steps that demands two symbols
; at every step reuses their two cells, and runs in well under a second.
; Were each demand to search the whole history, the loop would take minutes.
(= limit 60000)
(= step 1)
(= (count $n) (if (>= $n limit) $n (count (+ $n step))))
!(count 0)
