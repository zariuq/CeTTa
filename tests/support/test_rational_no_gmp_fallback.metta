; ENABLE_GMP=0 keeps default division HE-compatible and refuses the exact
; rational lane loudly instead of pretending GMP-backed rationals exist.

!(assertEqualToResult
  (/ 1 2)
  (0.5))

!(pragma! prefer-rationals True)

!(assertEqualToResult
  (/ 1 2)
  ((Error (/ 1 2) RationalArithmeticUnavailable)))
