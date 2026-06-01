; Rationals must not reach `%` through a lossy mpq_get_d/fmod coercion.

!(assertEqualToResult
  (% 1/2 2)
  ((Error (% 1/2 2) (BadArgType 1 Number Number))))

!(assertEqualToResult
  (% 1 1/2)
  ((Error (% 1 1/2) (BadArgType 2 Number Number))))

!(assertEqualToResult
  (% 3 2)
  (1))

!(assertEqualToResult
  (% 4/2 2)
  (0))
