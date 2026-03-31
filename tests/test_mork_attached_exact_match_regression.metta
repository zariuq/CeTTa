!(import! &self mork)

!(bind! &src (new-space hash))
!(space-set-match-backend! &src pathmap-imported)
!(add-atom &src (edge a b))
!(add-atom &src (edge a b))
!(add-atom &src (edge b c))
!(assertEqual
  (mork:dump! &src "../runtime/test_mork_attached_exact_match_regression.act")
  ())

!(bind! &compiled (mork:open-act "../runtime/test_mork_attached_exact_match_regression.act"))
!(assertEqualToResult
  (size &compiled)
  (3))
!(assertEqualToResult
  (match &compiled (edge a b) hit)
  (hit hit))
!(assertEqualToResult
  (match &compiled (edge $x $y) ($x $y))
  ((a b) (a b) (b c)))
