; Pathmap/MORK bridge result decoding must preserve rational tokens as exact
; Numbers, not symbols with numeric-looking names.

!(import! &self mork)

!(bind! &pathmap-rational (new-space pathmap))
!(add-atom &pathmap-rational (n 2/4))
!(assertEqualToResult
  (match &pathmap-rational (n $x) $x)
  (1/2))
!(assertEqualToResult
  (match &pathmap-rational (n $x) (get-type $x))
  (Number))

!(bind! &mork-rational (mork:new-space))
!(mork:add-atom &mork-rational (n 2/4))
!(assertEqualToResult
  (mork:match &mork-rational (n $x) $x)
  (1/2))
!(assertEqualToResult
  (mork:match &mork-rational (n $x) (get-type $x))
  (Number))

!(mork:dump! &mork-rational "../runtime/test_rational_bridge_roundtrip.act")
!(bind! &mork-rational-loaded (mork:new-space))
!(mork:load-act! &mork-rational-loaded "../runtime/test_rational_bridge_roundtrip.act")
!(assertEqualToResult
  (mork:match &mork-rational-loaded (n $x) $x)
  (1/2))
