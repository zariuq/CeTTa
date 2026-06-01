; Bigints are exact Numbers with canonical decimal printing.

!(assertEqualToResult
  (+ +0009223372036854775808 0)
  (9223372036854775808))

!(assertEqualToResult
  (+ 354224848179261915075 1)
  (354224848179261915076))

!(assertEqualToResult
  (- 354224848179261915075 75)
  (354224848179261915000))

!(assertEqualToResult
  (* 12345678901234567890 10)
  (123456789012345678900))

!(assertEqualToResult
  (/ 354224848179261915075 5)
  (70844969635852383015))

!(assertEqualToResult
  (/ 9223372036854775808 2)
  (4611686018427387904))

!(assertEqualToResult
  (/ 100000000000000000000000000 3)
  (3.3333333333333331e+25))

!(assertEqualToResult
  (/ -9223372036854775808 -1)
  (9223372036854775808))

!(assertEqualToResult
  (/ 5 2)
  (2.5))

!(assertEqualToResult
  (- (+ 9223372036854775807 10) 10)
  (9223372036854775807))

!(assertEqualToResult
  (% -9223372036854775809 10)
  (-9))

!(assertEqualToResult
  (% -9223372036854775808 -1)
  (0))

!(assertEqualToResult
  (> 354224848179261915075 9223372036854775807)
  (True))

!(assertEqualToResult
  (get-type 354224848179261915075)
  (Number))
