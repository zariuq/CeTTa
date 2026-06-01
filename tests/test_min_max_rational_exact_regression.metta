; min-atom/max-atom preserve exact winners unless a float contaminates the list.

!(pragma! prefer-rationals True)

!(assertEqualToResult
  (min-atom (1/2 2/3 3/4))
  (1/2))

!(assertEqualToResult
  (max-atom (1/2 2/3 3/4))
  (3/4))

!(assertEqualToResult
  (min-atom (100000000000000000000000000 7/2 4))
  (7/2))

!(assertEqualToResult
  (max-atom (100000000000000000000000000 7/2 4))
  (100000000000000000000000000))

!(min-atom (1/2 0.75 2/3))

!(max-atom (1/2 0.75 2/3))
