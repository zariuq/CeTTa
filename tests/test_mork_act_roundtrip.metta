!(import! &self mork)

!(bind! &src (new-space mork))
!(add-atom &src (edge a b))
!(add-atom &src (edge a b))
!(add-atom &src (edge b c))

!(assertEqual
  (mork:dump! &src "../runtime/test_mork_act_roundtrip.act")
  ())

!(bind! &dst (new-space mork))
!(assertEqual
  (mork:load-act! &dst "../runtime/test_mork_act_roundtrip.act")
  ())
!(assertEqualToResult
  (size &dst)
  (3))
!(assertEqualToResult
  (match &dst (edge a b) hit)
  (hit hit))
!(assertEqualToResult
  (match &dst (edge b c) hit)
  (hit))

!(assertEqual
  (import! &plain "../runtime/test_mork_act_roundtrip.act")
  ())
!(assertEqualToResult
  (size &plain)
  (3))
!(assertEqualToResult
  (match &plain (edge a b) hit)
  (hit hit))
!(assertEqualToResult
  (match &plain (edge b c) hit)
  (hit))
