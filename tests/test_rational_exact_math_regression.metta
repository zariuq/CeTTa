; Exact rationals stay exact through math operations that have exact rational results.

!(pragma! prefer-rationals True)

!(assertEqualToResult
  (abs-math -3/4)
  (3/4))

!(assertEqualToResult
  (trunc-math 7/3)
  (2))

!(assertEqualToResult
  (trunc-math -7/3)
  (-2))

!(assertEqualToResult
  (floor-math -7/3)
  (-3))

!(assertEqualToResult
  (ceil-math -7/3)
  (-2))

!(assertEqualToResult
  (round-math 5/2)
  (3))

!(assertEqualToResult
  (round-math -5/2)
  (-3))

!(assertEqualToResult
  (sqrt-math 1/4)
  (1/2))

! (sqrt-math 2/3)
