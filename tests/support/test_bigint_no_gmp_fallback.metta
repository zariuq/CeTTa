; ENABLE_GMP=0 keeps bigint atoms parseable and typed, while exact
; bigint arithmetic fails loudly instead of linking GMP implicitly.

!(assertEqualToResult
  (get-type 354224848179261915075)
  (Number))

!(assertEqualToResult
  (+ 9223372036854775807 1)
  ((Error (+ 9223372036854775807 1) BigintArithmeticUnavailable)))

!(assertEqualToResult
  (+ 354224848179261915075 1)
  ((Error (+ 354224848179261915075 1) BigintArithmeticUnavailable)))
