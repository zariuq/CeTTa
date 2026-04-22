!(import! &self mork)

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (row key "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail"))
!(assertEqualToResult
  (mork:size &ws)
  (1))
!(assertEqualToResult
  (mork:match &ws (row key $x) $x)
  ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail"))
!(assertEqual
  (collapse (mork:get-atoms &ws))
  ((row key "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail")))
!(bind! &z (mork:zipper &ws))
!(assertEqualToResult
  (mork:zipper-descend-until! &z)
  (True))
!(bind! &long-path
  (mork:path-of-atom (row key "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail")))
!(assertEqualToResult
  (mork:zipper-path-bytes &z)
  (&long-path))
!(assertEqualToResult
  (mork:zipper-close! &z)
  (()))
!(mork:remove-atom &ws (row key "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail"))
!(assertEqualToResult
  (mork:size &ws)
  (0))
!(mork:add-atoms &ws
  ((row key "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail")))
!(assertEqualToResult
  (mork:size &ws)
  (1))
!(assertEqual
  (collapse (mork:get-atoms &ws))
  ((row key "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail")))
