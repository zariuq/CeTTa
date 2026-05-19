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
!(bind! &pa (mork:new-space))
!(bind! &pb (mork:new-space))
!(bind! &pc (mork:new-space))
!(mork:add-atom &pa a)
!(mork:add-atom &pa d)
!(mork:add-atom &pb b)
!(mork:add-atom &pc c)
!(bind! &pnav (mork:product-zipper &pa &pb &pc))
!(assertEqualToResult
  (mork:product-zipper-descend-first! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-descend-last! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pnav)
  ((193 100)))
!(assertEqualToResult
  (mork:product-zipper-ascend-until-branch! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pnav)
  ((193)))
!(assertEqualToResult
  (mork:product-zipper-descend-first! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-next-sibling-byte! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pnav)
  ((193 100)))
!(assertEqualToResult
  (mork:product-zipper-prev-sibling-byte! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pnav)
  ((193 97)))
!(assertEqualToResult
  (mork:product-zipper-reset! &pnav)
  (()))
!(assertEqualToResult
  (mork:product-zipper-descend-until-max-bytes! &pnav 1)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pnav)
  ((193)))
!(assertEqualToResult
  (mork:product-zipper-reset! &pnav)
  (()))
!(assertEqualToResult
  (mork:product-zipper-next-step! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pnav)
  ((193)))
!(assertEqualToResult
  (mork:product-zipper-reset! &pnav)
  (()))
!(assertEqualToResult
  (mork:product-zipper-next-val! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pnav)
  ((193 97)))
!(assertEqualToResult
  (mork:product-zipper-next-val! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pnav)
  ((193 97 193 98)))
!(assertEqualToResult
  (mork:product-zipper-next-val! &pnav)
  (True))
!(assertEqualToResult
  (mork:product-zipper-path-bytes &pnav)
  ((193 97 193 98 193 99)))
!(assertEqualToResult
  (mork:product-zipper-close! &pnav)
  (()))
