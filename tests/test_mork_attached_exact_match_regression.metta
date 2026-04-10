!(import! &self mork)

!(bind! &src (mork:new-space hash))
!(mork:add-atom &src (edge a b))
!(mork:add-atom &src (edge a b))
!(mork:add-atom &src (edge b c))
!(assertEqual
  (mork:dump! &src "../runtime/test_mork_attached_exact_match_regression.act")
  ())

!(bind! &compiled (mork:open-act "../runtime/test_mork_attached_exact_match_regression.act"))
!(assertEqualToResult
  (mork:size &compiled)
  (2))
!(assertEqualToResult
  (mork:match &compiled (edge a b) hit)
  (hit))
!(assertEqualToResult
  (mork:match &compiled (edge $x $y) ($x $y))
  ((a b) (b c)))
