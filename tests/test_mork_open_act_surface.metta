!(import! &self mork)

!(bind! &src (mork:new-space))
!(mork:add-atom &src (edge a b))
!(mork:add-atom &src (edge a b))
!(mork:add-atom &src (edge b c))
!(assertEqual
  (mork:dump! &src "../runtime/test_mork_open_act_surface.act")
  ())

!(bind! &compiled (mork:open-act "../runtime/test_mork_open_act_surface.act"))
!(assertEqualToResult
  (get-type &compiled)
  (MorkSpace))
!(assertEqualToResult
  (mork:size &compiled)
  (2))
!(assertEqualToResult
  (mork:match &compiled (edge a b) hit)
  (hit))
!(assertEqualToResult
  (mork:match &compiled (edge b c) hit)
  (hit))
!(mork:add-atom &compiled (edge c d))
!(assertEqualToResult
  (mork:size &compiled)
  (3))
!(assertEqualToResult
  (mork:match &compiled (edge c d) hit)
  (hit))
!(assertEqual
  (collapse (mork:get-atoms &compiled))
  ((edge a b) (edge b c) (edge c d)))
; Note: collapse returns a single list, so assertEqual compares one value — safe.
