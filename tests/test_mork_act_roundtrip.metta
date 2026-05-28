!(import! &self mork)
!(import! &self fs)

!(bind! &src (mork:new-space))
!(mork:add-atom &src (edge a b))
!(mork:add-atom &src (edge a b))
!(mork:add-atom &src (edge b c))

!(assertEqual
  (mork:dump! &src "../runtime/test_mork_act_roundtrip.act")
  ())

!(bind! &dst (mork:new-space))
!(assertEqual
  (mork:load-act! &dst "../runtime/test_mork_act_roundtrip.act")
  ())
!(assertEqualToResult
  (mork:size &dst)
  (2))
!(assertEqualToResult
  (mork:match &dst (edge a b) hit)
  (hit))
!(assertEqualToResult
  (mork:match &dst (edge b c) hit)
  (hit))

!(bind! &dst-additive (mork:new-space))
!(mork:add-atom &dst-additive (seed keep))
!(assertEqual
  (mork:load-act! &dst-additive "../runtime/test_mork_act_roundtrip.act")
  ())
!(assertEqualToResult
  (mork:size &dst-additive)
  (3))
!(assertEqualToResult
  (mork:match &dst-additive (seed keep) hit)
  (hit))
!(assertEqualToResult
  (mork:match &dst-additive (edge a b) hit)
  (hit))
!(assertEqualToResult
  (mork:match &dst-additive (edge b c) hit)
  (hit))

!(bind! &dst-include (mork:new-space))
!(mork:add-atom &dst-include (seed include))
!(assertEqual
  (mork:include! &dst-include "../runtime/test_mork_act_roundtrip.act")
  ())
!(assertEqualToResult
  (mork:size &dst-include)
  (3))
!(assertEqualToResult
  (mork:match &dst-include (seed include) hit)
  (hit))
!(assertEqualToResult
  (mork:match &dst-include (edge a b) hit)
  (hit))
!(assertEqualToResult
  (mork:match &dst-include (edge b c) hit)
  (hit))

!(assertEqual
  (fs:write-text "runtime/test_mork_act_roundtrip.act.copies" "legacy sidecar")
  ())
!(assertEqual
  (fs:exists "runtime/test_mork_act_roundtrip.act.copies")
  True)
!(bind! &dst-legacy (mork:new-space))
!(mork:add-atom &dst-legacy (seed legacy))
!(assertEqualToResult
  (let $ignored (collapse (mork:load-act! &dst-legacy "../runtime/test_mork_act_roundtrip.act"))
    (mork:size &dst-legacy))
  (1))
!(assertEqualToResult
  (mork:match &dst-legacy (seed legacy) hit)
  (hit))
!(assertEqualToResult
  (mork:match &dst-legacy (edge a b) hit)
  ())
!(assertEqual
  (mork:dump! &src "../runtime/test_mork_act_roundtrip.act")
  ())
!(assertEqual
  (fs:exists "runtime/test_mork_act_roundtrip.act.copies")
  False)

!(assertEqual
  (import! &plain "../runtime/test_mork_act_roundtrip.act")
  ())
!(assertEqualToResult
  (size &plain)
  (2))
!(assertEqualToResult
  (match &plain (edge a b) hit)
  (hit))
!(assertEqualToResult
  (match &plain (edge b c) hit)
  (hit))
