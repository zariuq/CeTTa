; A depth cutoff is not an exhausted collection.  The depth exceeds the
; initial first-witness portfolio quantum.
(= (deep $n) (if (> $n 0) (deep (- $n 1)) ok))
(= (observed-count $n) (size-atom (collapse (deep $n))))
(= (observed-empty $n) (== () (collapse (deep $n))))
!(once (observed-count 20000))
!(once (observed-empty 20000))

; A commit reached after a deep prefix still removes later equations.
(= (committed $n) (let $x (deep $n) (let $c (cut) (empty))))
(= (committed $n) forbidden)
!(collapse (once (committed 20000)))

; The body of a nested once retains its commit before its continuation.
(= (nested $n) (let $x (once (deep $n)) (if (== $x ok) 7 9)))
!(once (nested 20000))

; A dynamically selected relation is checked at entry as well.
(= (apply-observer $f $n) ($f $n))
!(once (apply-observer observed-count 20000))
