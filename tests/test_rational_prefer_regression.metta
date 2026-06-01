; prefer-rationals upgrades exact/exact division to the canonical exact tower.

!(pragma! prefer-rationals True)

!(/ 10 3)
!(/ 100000000000000000000000000 3)

!(assertEqualToResult
  (+ 1/3 1/6)
  (1/2))

!(assertEqualToResult
  (* 1/3 6)
  (2))

!(assertEqualToResult
  (/ 1/2 2)
  (1/4))

!(assertEqualToResult
  (/ -2 -4)
  (1/2))

!(assertEqualToResult
  (/ 0 7)
  (0))

!(assertEqualToResult
  (== 2/4 1/2)
  (True))

!(assertEqualToResult
  (== 4/2 2)
  (True))

!(assertEqualToResult
  (+ 1/2 0.25)
  (0.75))
