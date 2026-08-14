; Roman specializes a singleton open type candidate when the later repeated
; head occurrence supplies Number.  The body may then consume the variable as
; Number without a residual guard.
(: repeated-head-open-first (-> $a Number Number))
(= (repeated-head-open-first $value $value) (+ $value 1))

!(test (repeated-head-open-first 5 5) 6)
