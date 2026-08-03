(= (assert-equal-local $actual $expected)
   (assert (== $actual $expected)))

!(assert-equal-local 1 2)
!(println! should-not-run)
