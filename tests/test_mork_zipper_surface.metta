!(include mork)
!(bind! &kb (mork:new-space))
!(assertEqualToResult
  (get-type &kb)
  (MorkSpace))
!(mork:add-atom &kb (edge a b))
!(mork:add-atom &kb (edge a c))
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
!(bind! &root-copy (mork:zipper-subspace &z))
!(assertEqualToResult
  (get-type &root-copy)
  (MorkSpace))
!(assertEqualToResult
  (mork:size &root-copy)
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
  (mork:zipper-depth &zf)
  (0))
!(assertEqualToResult
  (mork:zipper-close! &zf)
  (()))
!(assertEqualToResult
  (mork:zipper-reset! &z)
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
