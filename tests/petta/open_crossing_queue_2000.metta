; A tier loop threads a growing queue through a host goal each step:
; Explicit eval crosses into the host; typed calls alone can now run in
; the open tier. Authored links preserve tails through both evaluators;
; PeTTa's flat list cons would itself rebuild a growing list in the host.
(= (enqueue $E (queue $In $Out $N)) (queue (node $E $In) $Out (+ $N 1)))
(= (empty-queue) (queue () () 0))
(: push-both (-> Expression Expression Expression))
(= (push-both $items $Q) (eval (foldl enqueue $items $Q)))
(= (grow $Q $i $n)
   (if (== $i $n) $Q (grow (push-both ($i (+ $i 1)) $Q) (+ $i 1) $n)))
(= (node-count () $count) $count)
(= (node-count (node $item $tail) $count) (node-count $tail (+ $count 1)))
(= (qsize (queue $In $Out $N))
   (if (== $N (node-count $In 0)) $N broken-queue))
!(qsize (grow (empty-queue) 0 2000))
