; Host values owned by the PeTTa dialect stay native.  These witnesses cover
; time, formatting, inclusive integer bounds, half-open float bounds, the
; accepted &rng spelling, and negative cases that must produce no result.
!(> (current-time) 1700000000)
!(let* (($first (current-time)) ($second (current-time))) (>= $second $first))
!(format-time "literal")
!(format-time "%%")
!(random-int 4 4)
!(random-int &rng -2 -2)
!(random-float 1.5 1.5)
!(random-float &rng 1.5 1.5)
!(let $value (random-int -2 2) (and (>= $value -2) (<= $value 2)))
!(let $value (random-float 1.0 2.0) (and (>= $value 1.0) (< $value 2.0)))
!(let $value (random-float 2.0 1.0) (and (> $value 1.0) (<= $value 2.0)))
!(collapse (random-int 2 1))
!(collapse (random-int not-rng 4 4))
