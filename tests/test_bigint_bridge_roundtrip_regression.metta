; Pathmap/MORK bridge result decoding must preserve overflow-sized integer
; tokens as bigint Numbers, not symbols with numeric-looking names.

!(import! &self mork)

!(bind! &pathmap-bigint (new-space pathmap))
!(add-atom &pathmap-bigint (n 354224848179261915075))
!(assertEqualToResult
  (match &pathmap-bigint (n $x) $x)
  (354224848179261915075))
!(assertEqualToResult
  (match &pathmap-bigint (n $x) (get-type $x))
  (Number))

!(bind! &mork-bigint (mork:new-space))
!(mork:add-atom &mork-bigint (n 354224848179261915075))
!(assertEqualToResult
  (mork:match &mork-bigint (n $x) $x)
  (354224848179261915075))
!(assertEqualToResult
  (mork:match &mork-bigint (n $x) (get-type $x))
  (Number))

!(mork:dump! &mork-bigint "../runtime/test_bigint_bridge_roundtrip.act")
!(bind! &mork-bigint-loaded (mork:new-space))
!(mork:load-act! &mork-bigint-loaded "../runtime/test_bigint_bridge_roundtrip.act")
!(assertEqualToResult
  (mork:match &mork-bigint-loaded (n $x) $x)
  (354224848179261915075))
