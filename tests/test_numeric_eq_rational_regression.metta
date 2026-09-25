; `==` compares numbers by exact value across representations, so 1/2
; equals 0.5; `numeric-eq` is the same comparison restricted to numbers.

!(pragma! prefer-rationals True)

!(assertEqualToResult
  (== 1/2 0.5)
  (True))

!(assertEqualToResult
  (numeric-eq 1/2 0.5)
  (True))

!(assertEqualToResult
  (numeric-eq 1/2 2/4)
  (True))

!(assertEqualToResult
  (numeric-eq 100000000000000000000000000 100000000000000000000000000/1)
  (True))

!(assertEqualToResult
  (numeric-eq 1/3 0.3333333333333334)
  (False))

!(assertEqualToResult
  (<= 1/2 0.5)
  (True))

!(assertEqualToResult
  (>= 1/2 0.5)
  (True))
