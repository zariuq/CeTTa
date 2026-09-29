; A tier loop threads a growing queue through a host goal each step:
; `push-both` is typed, so the host runs it (a typed call's demand follows
; its declared type), and `grow` runs in the open tier because `qsize`
; demands its destination.  Each step crosses the boundary twice.
(= (enqueue $E (queue $In $Out $N)) (queue (cons $E $In) $Out (+ $N 1)))
(= (empty-queue) (queue () () 0))
(: push-both (-> Expression Expression Expression))
(= (push-both $items $Q) (foldl enqueue $items $Q))
(= (grow $Q $i $n)
   (if (== $i $n) $Q (grow (push-both ($i (+ $i 1)) $Q) (+ $i 1) $n)))
(= (qsize (queue $In $Out $N)) $N)
!(qsize (grow (empty-queue) 0 1000))
