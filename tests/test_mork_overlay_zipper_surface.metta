!(include mork)
!(bind! &base (mork:new-space))
!(bind! &overlay (mork:new-space))
!(mork:add-atom &base a)
!(mork:add-atom &overlay (edge a b))
!(bind! &oz (mork:overlay-zipper &base &overlay))
!(assertEqualToResult
  (mork:overlay-zipper-child-count &oz)
  (2))
!(assertEqualToResult
  (mork:overlay-zipper-child-bytes &oz)
  ((3 193)))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &oz)
  (()))
!(assertEqualToResult
  (mork:overlay-zipper-descend-index! &oz 0)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &oz)
  ((3)))
!(assertEqualToResult
  (mork:overlay-zipper-path-exists &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-is-val &oz)
  (False))
!(assertEqualToResult
  (mork:overlay-zipper-reset! &oz)
  (()))
!(assertEqualToResult
  (mork:overlay-zipper-descend-index! &oz 1)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &oz)
  ((193)))
!(assertEqualToResult
  (mork:overlay-zipper-path-exists &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-reset! &oz)
  (()))
!(assertEqualToResult
  (mork:overlay-zipper-next-step! &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &oz)
  ((3)))
!(assertEqualToResult
  (mork:overlay-zipper-next-sibling-byte! &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &oz)
  ((193)))
!(assertEqualToResult
  (mork:overlay-zipper-prev-sibling-byte! &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &oz)
  ((3)))
!(assertEqualToResult
  (mork:overlay-zipper-reset! &oz)
  (()))
!(assertEqualToResult
  (mork:overlay-zipper-descend-last! &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &oz)
  ((193)))
!(assertEqualToResult
  (mork:overlay-zipper-close! &oz)
  (()))
!(bind! &lhs (mork:new-space))
!(bind! &rhs (mork:new-space))
!(mork:add-atom &lhs (edge a c))
!(mork:add-atom &rhs (edge a b))
!(bind! &shared (mork:overlay-zipper &lhs &rhs))
!(assertEqualToResult
  (mork:overlay-zipper-child-count &shared)
  (1))
!(assertEqualToResult
  (mork:overlay-zipper-descend-until! &shared)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &shared)
  ((3 196 101 100 103 101 193 97 193)))
!(assertEqualToResult
  (mork:overlay-zipper-path-exists &shared)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-child-count &shared)
  (2))
!(assertEqualToResult
  (mork:overlay-zipper-is-val &shared)
  (False))
!(assertEqualToResult
  (mork:overlay-zipper-depth &shared)
  (9))
!(assertEqualToResult
  (mork:overlay-zipper-ascend! &shared 9)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-descend-until-max-bytes! &shared 2)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &shared)
  ((3 196)))
!(assertEqualToResult
  (mork:overlay-zipper-reset! &shared)
  (()))
!(assertEqualToResult
  (mork:overlay-zipper-descend-until! &shared)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-ascend-until-branch! &shared)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &shared)
  (()))
!(assertEqualToResult
  (mork:overlay-zipper-close! &shared)
  (()))
