; Explicit control over Prime's branch-local Need heap.  delay and resample
; are canonical syntax; Promise and Resampler are episode-private values and
; must serialize back to these forms at an observation boundary.

!(delay (+ 1 2))
!(resample (+ 1 2))
!(force (delay (superpose (A B))))
!(if-error (== 1 "bad") caught no-error)

; A chosen branch is fully observed at the command boundary, while the
; unchosen branch remains suspended.
!(if True (Box (+ 1 2)) (+ "boom" 1))
!(if False (+ "boom" 1) (Box (+ 2 3)))

; A single promise memoizes the chosen value in each branch.
!(let $p (delay (superpose (A B)))
   (Pair (force $p) (force $p)))

; A resampler deliberately opts out of call-time sharing.
!(let $r (resample (superpose (A B)))
   (Pair (force $r) (force $r)))

; Effects are receipts for the distinction: a promise performs one update,
; while a resampler performs one update per force.  Neither branch receipt is
; implicitly committed to the ambient state cell.
!(bind! &prime-need:delay-counter (new-state 0))
(= (prime-need:delay-bump)
   (let $old (get-state &prime-need:delay-counter)
     (let $_ (change-state! &prime-need:delay-counter (+ $old 1))
       $old)))
!(let $p (delay (prime-need:delay-bump))
   (Pair (force $p) (force $p)))
!(get-state &prime-need:delay-counter)

!(bind! &prime-need:resample-counter (new-state 0))
(= (prime-need:resample-bump)
   (let $old (get-state &prime-need:resample-counter)
     (let $_ (change-state! &prime-need:resample-counter (+ $old 1))
       $old)))
!(let $r (resample (prime-need:resample-bump))
   (Pair (force $r) (force $r)))
!(get-state &prime-need:resample-counter)

; A caught fault retains the promise's refined branch heap.  Repeating the
; force observes the cached fault receipt and does not repeat its effect; the
; ambient state remains unchanged.
!(bind! &prime-need:fault-counter (new-state 0))
(= (prime-need:fault-bump)
   (let $old (get-state &prime-need:fault-counter)
     (let $_ (change-state! &prime-need:fault-counter (+ $old 1))
       (== 1 "bad"))))
!(let $p (delay (prime-need:fault-bump))
   (Pair (if-error (force $p) caught no-error)
         (if-error (force $p) caught no-error)))
!(get-state &prime-need:fault-counter)

; Fault propagation is a runtime observation.  Repeated-fault identity and
; memoization are pinned independently by the native heap-algebra gate.
!(force (delay (== 1 "bad")))
