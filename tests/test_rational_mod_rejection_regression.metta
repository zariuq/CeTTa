; Rationals reach `%` exactly, never through a lossy mpq_get_d/fmod
; coercion: `%` is the remainder of floor division, a - b * floor(a / b).

!(assertEqualToResult
  (% 1/2 2)
  (1/2))

!(assertEqualToResult
  (% 1 1/2)
  (0))

!(assertEqualToResult
  (% 3 2)
  (1))

!(assertEqualToResult
  (% 4/2 2)
  (0))

!(assertEqualToResult
  (% -7/2 3)
  (5/2))

!(assertEqualToResult
  (% 7/2 -3)
  (-5/2))
