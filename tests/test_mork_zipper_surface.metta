!(include mork)
!(bind! &kb (mork:new-space))
!(assertEqualToResult
  (get-type &kb)
  (MorkSpace))
!(mork:add-atom &kb (edge a b))
!(mork:add-atom &kb (edge a c))
!(bind! &string-kb (mork:new-space))
!(mork:add-atom &string-kb "solo")
!(bind! &string-z (mork:zipper &string-kb))
!(assertEqualToResult
  (mork:zipper-descend-until! &string-z)
  (True))
!(bind! &solo-path (mork:path-of-atom "solo"))
!(assertEqualToResult
  (mork:zipper-path-bytes &string-z)
  (&solo-path))
!(bind! &z (mork:zipper &kb))
!(assertEqualToResult
  (mork:path-of-atom (edge a b))
  ((3 196 101 100 103 101 193 97 193 98)))
!(bind! &root_children (mork:zipper-child-bytes &z))
!(assertEqualToResult
  (mork:zipper-child-bytes &z)
  ((3)))
!(assertEqualToResult
  (index-atom &root_children 0)
  (3))
!(assertEqualToResult
  (mork:zipper-path-exists &z)
  (True))
!(assertEqualToResult
  (mork:zipper-is-val &z)
  (False))
!(assertEqualToResult
  (mork:zipper-val-count &z)
  (2))
!(assertEqualToResult
  (mork:zipper-depth &z)
  (0))
!(assertEqualToResult
  (mork:zipper-descend-first! &z)
  (True))
!(assertEqualToResult
  (mork:zipper-depth &z)
  (1))
!(assertEqualToResult
  (mork:zipper-path-bytes &z)
  ((3)))
!(assertEqualToResult
  (mork:zipper-ascend! &z 1)
  (True))
!(assertEqualToResult
  (mork:zipper-depth &z)
  (0))
!(assertEqualToResult
  (mork:zipper-descend-index! &z 0)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &z)
  ((3)))
!(assertEqualToResult
  (mork:zipper-ascend! &z 1)
  (True))
!(bind! &root-map (mork:zipper-make-map &z))
!(assertEqualToResult
  (get-type &root-map)
  (MorkSpace))
!(assertEqualToResult
  (mork:size &root-map)
  (2))
!(bind! &root-snap (mork:zipper-make-snapshot-map &z))
!(assertEqualToResult
  (get-type &root-snap)
  (MorkSpace))
!(assertEqualToResult
  (mork:size &root-snap)
  (2))
!(assertEqualToResult
  (mork:zipper-descend-until! &z)
  (True))
!(assertEqualToResult
  (mork:zipper-path-exists &z)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &z)
  ((3 196 101 100 103 101 193 97 193)))
!(bind! &zf (mork:zipper-fork &z))
!(assertEqualToResult
  (mork:zipper-path-exists &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &zf)
  ((3 196 101 100 103 101 193 97 193)))
!(assertEqualToResult
  (mork:zipper-depth &zf)
  (9))
!(assertEqualToResult
  (mork:zipper-descend-last! &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &zf)
  ((3 196 101 100 103 101 193 97 193 99)))
!(assertEqualToResult
  (mork:zipper-prev-sibling-byte! &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &zf)
  ((3 196 101 100 103 101 193 97 193 98)))
!(assertEqualToResult
  (mork:zipper-next-sibling-byte! &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &zf)
  ((3 196 101 100 103 101 193 97 193 99)))
!(assertEqualToResult
  (mork:zipper-ascend-until-branch! &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &zf)
  ((3 196 101 100 103 101 193 97 193)))
!(assertEqualToResult
  (mork:zipper-descend-first! &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-next-step! &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &zf)
  ((3 196 101 100 103 101 193 97 193 99)))
!(assertEqualToResult
  (mork:zipper-ascend-until-branch! &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-next-val! &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &zf)
  ((3 196 101 100 103 101 193 97 193 98)))
!(assertEqualToResult
  (mork:zipper-next-val! &zf)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &zf)
  ((3 196 101 100 103 101 193 97 193 99)))
!(assertEqualToResult
  (mork:zipper-close! &zf)
  (()))
!(assertEqualToResult
  (mork:zipper-reset! &z)
  (()))
!(bind! &zt (mork:zipper-fork &z))
!(assertEqualToResult
  (mork:zipper-descend-until-max-bytes! &zt 1)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &zt)
  ((3)))
!(assertEqualToResult
  (mork:zipper-close! &zt)
  (()))
!(assertEqualToResult
  (mork:zipper-depth &z)
  (0))
!(bind! &mw (mork:new-space))
!(mork:add-atom &mw (edge x y))
!(bind! &mz (mork:zipper &mw))
!(assertEqualToResult
  (mork:path-of-atom (edge x y))
  ((3 196 101 100 103 101 193 120 193 121)))
!(assertEqualToResult
  (mork:zipper-val-count &mz)
  (1))
!(assertEqualToResult
  (mork:zipper-descend-until! &mz)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &mz)
  ((3 196 101 100 103 101 193 120 193 121)))
!(assertEqualToResult
  (mork:zipper-close! &mz)
  (()))
!(assertEqualToResult
  (mork:zipper-close! &z)
  (()))
