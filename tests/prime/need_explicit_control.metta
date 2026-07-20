; Explicit control over Prime's branch-local Need heap.  Delay and Resample
; are canonical syntax; Promise and Resampler are episode-private values and
; must serialize back to these forms at an observation boundary.

!(Delay (+ 1 2))
!(Resample (+ 1 2))
!(Force (Delay (superpose (A B))))
!(if-error (== 1 "bad") caught no-error)

; A single promise memoizes the chosen value in each branch.
!(let $p (Delay (superpose (A B)))
   (Pair (Force $p) (Force $p)))

; A resampler deliberately opts out of call-time sharing.
!(let $r (Resample (superpose (A B)))
   (Pair (Force $r) (Force $r)))

; Effects are receipts for the distinction: a promise performs one update,
; while a resampler performs one update per force.
!(bind! &prime-need:delay-counter (new-state 0))
(= (prime-need:delay-bump)
   (let $old (get-state &prime-need:delay-counter)
     (let $_ (change-state! &prime-need:delay-counter (+ $old 1))
       $old)))
!(let $p (Delay (prime-need:delay-bump))
   (Pair (Force $p) (Force $p)))
!(get-state &prime-need:delay-counter)

!(bind! &prime-need:resample-counter (new-state 0))
(= (prime-need:resample-bump)
   (let $old (get-state &prime-need:resample-counter)
     (let $_ (change-state! &prime-need:resample-counter (+ $old 1))
       $old)))
!(let $r (Resample (prime-need:resample-bump))
   (Pair (Force $r) (Force $r)))
!(get-state &prime-need:resample-counter)

; A caught fault retains the promise's refined branch heap.  Repeating the
; force observes the cached fault receipt and does not repeat its effect.
!(bind! &prime-need:fault-counter (new-state 0))
(= (prime-need:fault-bump)
   (let $old (get-state &prime-need:fault-counter)
     (let $_ (change-state! &prime-need:fault-counter (+ $old 1))
       (== 1 "bad"))))
!(let $p (Delay (prime-need:fault-bump))
   (Pair (if-error (Force $p) caught no-error)
         (if-error (Force $p) caught no-error)))
!(get-state &prime-need:fault-counter)

; Fault propagation is a runtime observation.  Repeated-fault identity and
; memoization are pinned independently by the native heap-algebra gate.
!(Force (Delay (== 1 "bad")))
