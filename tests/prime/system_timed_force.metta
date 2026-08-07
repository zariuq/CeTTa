!(import! &self system)

; `delay` is the visible suspension marker; `system:timed` both demands it and
; returns the selected value with an elapsed-nanosecond observation.
!(case (system:timed (delay (+ 1 2)))
   (((system:timed-result $elapsed 3)
     (and (<= 0 $elapsed) (== (get-type $elapsed) Number)))
    ($_ False)))

; Without an explicit suspension there is no hidden eager coercion: `force`
; remains uninterpreted on ordinary data, following MeTTa's inertness rule.
!(case (system:timed 5)
   (((system:timed-result $elapsed (force 5)) (<= 0 $elapsed))
    ($_ False)))

; Timing does not change nondeterministic answer multiplicity.
!(case (system:timed (delay (superpose (A B))))
   (((system:timed-result $elapsed $value)
     (TimedValue (<= 0 $elapsed) $value))
    ($_ malformed)))
