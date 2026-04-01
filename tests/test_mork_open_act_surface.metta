!(import! &self mork)

!(bind! &src (new-space mork))
!(add-atom &src (edge a b))
!(add-atom &src (edge a b))
!(add-atom &src (edge b c))
!(assertEqual
  (mork:dump! &src "../runtime/test_mork_open_act_surface.act")
  ())

!(bind! &compiled (mork.open-act "../runtime/test_mork_open_act_surface.act"))
!(assertEqualToResult
  (size &compiled)
  (3))
!(assertEqualToResult
  (match &compiled (edge a b) hit)
  (hit hit))
!(assertEqualToResult
  (match &compiled (edge b c) hit)
  (hit))
!(add-atom &compiled (edge c d))
!(assertEqualToResult
  (size &compiled)
  (4))
!(assertEqualToResult
  (match &compiled (edge c d) hit)
  (hit))
!(assertEqual
  (collapse (get-atoms &compiled))
  ((edge a b) (edge a b) (edge b c) (edge c d)))
; Note: collapse returns a single list, so assertEqual compares one value — safe.
