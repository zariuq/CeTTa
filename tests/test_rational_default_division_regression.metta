; Default arithmetic remains HE-compatible: non-exact integer division returns
; a float, while explicit rational literals stay exact Numbers.

!(/ 10 3)
!(/ 100000000000000000000000000 3)

!(assertEqualToResult
  (+ 1/3 1/6)
  (1/2))

!(assertEqualToResult
  (get-type 1/2)
  (Number))
