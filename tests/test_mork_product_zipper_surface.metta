!(include mork)
!(bind! &a (mork:new-space))
!(bind! &b (mork:new-space))
!(bind! &c (mork:new-space))
!(mork:add-atom &a a)
!(mork:add-atom &b b)
!(mork:add-atom &c c)
!(bind! &pz (mork:product-zipper &a &b &c))
!(assertEqualToResult
  (mork:product-zipper-factor-count &pz)
  (3))
!(assertEqualToResult
  (mork:product-zipper-focus-factor &pz)
  (0))
!(assertEqualToResult
  (mork:product-zipper-child-count &pz)
  (1))
!(assertEqualToResult
  (mork:product-zipper-child-bytes &pz)
  ((193)))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pz)
  (()))
!(assertEqualToResult
  (mork:product-zipper-path-indices &pz)
  (()))
!(assertEqualToResult
  (mork:product-zipper-path-exists &pz)
  (True))
!(assertEqualToResult
  (mork:product-zipper-is-val &pz)
  (False))
!(assertEqualToResult
  (mork:product-zipper-descend-first! &pz)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pz)
  ((193)))
!(assertEqualToResult
  (mork:product-zipper-descend-until! &pz)
  (True))
!(assertEqualToResult
  (mork:product-zipper-focus-factor &pz)
  (0))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pz)
  ((193 97)))
!(assertEqualToResult
  (mork:product-zipper-path-indices &pz)
  ((2)))
!(assertEqualToResult
  (mork:product-zipper-child-bytes &pz)
  ((193)))
!(assertEqualToResult
  (mork:product-zipper-descend-until! &pz)
  (True))
!(assertEqualToResult
  (mork:product-zipper-focus-factor &pz)
  (1))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pz)
  ((193 97 193 98)))
!(assertEqualToResult
  (mork:product-zipper-path-indices &pz)
  ((2 4)))
!(assertEqualToResult
  (mork:product-zipper-descend-until! &pz)
  (True))
!(assertEqualToResult
  (mork:product-zipper-focus-factor &pz)
  (2))
!(assertEqualToResult
  (mork:product-zipper-is-val &pz)
  (True))
!(assertEqualToResult
  (mork:product-zipper-val-count &pz)
  (1))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pz)
  ((193 97 193 98 193 99)))
!(assertEqualToResult
  (mork:product-zipper-path-indices &pz)
  ((2 4)))
!(assertEqualToResult
  (mork:product-zipper-reset! &pz)
  (()))
!(assertEqualToResult
  (mork:product-zipper-depth &pz)
  (0))
!(assertEqualToResult
  (mork:product-zipper-close! &pz)
  (()))
!(bind! &d (mork:new-space))
!(mork:add-atom &d d)
!(bind! &pz4 (mork:product-zipper &a &b &c &d))
!(assertEqualToResult
  (mork:product-zipper-factor-count &pz4)
  (4))
!(assertEqualToResult
  (mork:product-zipper-child-bytes &pz4)
  ((193)))
!(assertEqualToResult
  (mork:product-zipper-close! &pz4)
  (()))
