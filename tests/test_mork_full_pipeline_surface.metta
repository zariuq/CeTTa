!(include mork)

!(bind! &live (mork:new-space))
!(mork:add-atom &live (edge a b))
!(mork:add-atom &live (edge a c))
!(assertEqual
  (mork:dump! &live "../runtime/test_mork_full_pipeline_surface.act")
  ())

!(bind! &compiled (mork:open-act "../runtime/test_mork_full_pipeline_surface.act"))
!(assertEqualToResult
  (get-type &compiled)
  (MorkSpace))
!(assertEqualToResult
  (mork:size &compiled)
  (2))

!(bind! &loaded (mork:new-space))
!(assertEqual
  (mork:load-act! &loaded "../runtime/test_mork_full_pipeline_surface.act")
  ())
!(assertEqualToResult
  (mork:size &loaded)
  (2))

!(bind! &selector (mork:new-space))
!(mork:add-atom &selector (edge a b))
!(bind! &restricted (mork:prefix-restrict &compiled &selector))
!(assertEqualToResult
  (get-type &restricted)
  (MorkSpace))
!(assertEqualToResult
  (mork:size &restricted)
  (1))
!(assertEqualToResult
  (mork:match &restricted (edge a b) ok)
  (ok))

!(bind! &path (mork:path-of-atom (edge a b)))
!(assertEqualToResult
  &path
  ((3 196 101 100 103 101 193 97 193 98)))

!(bind! &z (mork:zipper &restricted))
!(assertEqualToResult
  (mork:zipper-descend-until! &z)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &z)
  (&path))
!(assertEqualToResult
  (mork:zipper-is-val &z)
  (True))
!(assertEqualToResult
  (mork:zipper-close! &z)
  (()))

!(bind! &oz (mork:overlay-zipper &restricted &selector))
!(assertEqualToResult
  (mork:overlay-zipper-descend-until! &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &oz)
  (&path))
!(assertEqualToResult
  (mork:overlay-zipper-path-exists &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-is-val &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-close! &oz)
  (()))
