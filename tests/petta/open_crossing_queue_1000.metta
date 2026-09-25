; A tier loop threads a growing queue through a host goal each step:
; `foldl` is the host's, and `grow` runs in the open tier because
; `qsize` demands its destination.  Each step crosses the boundary twice.
(= (enqueue $E (queue $In $Out $N)) (queue (cons $E $In) $Out (+ $N 1)))
(= (empty-queue) (queue () () 0))
(= (grow $Q $i $n)
   (if (== $i $n) $Q (grow (foldl enqueue ($i (+ $i 1)) $Q) (+ $i 1) $n)))
(= (qsize (queue $In $Out $N)) $N)
!(qsize (grow (empty-queue) 0 1000))
