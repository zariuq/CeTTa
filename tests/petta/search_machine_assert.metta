(= (assert-equal-local $actual $expected)
   (assert (== $actual $expected)))

!(test (assert-equal-local 1 1) True)

; Assertion evaluates its condition through PeTTa before the shared host
; reports success or failure.  Quoted syntax containing a strict operator
; must therefore reach alpha equality without being evaluated as arithmetic.
!(test (assert (=alpha (quote (+ $x $y))
                       (quote (+ $a $b))))
       True)
!(test (=alpha (quote (+ $x $x))
                (quote (+ $a $b)))
       False)
