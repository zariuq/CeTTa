!(import! &self mork)
!(bind! &h (mork:new-space))
!(assertEqualToResult
  (get-type &h)
  (MorkSpace))
!(assertEqualToResult
  (mork:size &h)
  (0))
!(mork:add-atom &h (edge a b))
!(mork:add-atom &h (edge a b))
!(mork:add-atom &h (edge b c))
!(assertEqualToResult
  (mork:size &h)
  (2))
!(assertEqualToResult
  (collapse (mork:match &h (edge a b) hit))
  ((hit)))
!(assertEqual
  (mork:match &h (edge $x $y) ($x $y))
  (superpose ((a b) (b c))))
!(assertEqual
  (collapse (mork:get-atoms &h))
  ((edge a b) (edge b c)))
!(bind! &snap (mork:clone &h))
!(mork:add-atom &h (edge c d))
!(assertEqual
  (collapse (mork:get-atoms &snap))
  ((edge a b) (edge b c)))
!(assertEqual
  (mork:match &h (edge $x $y) ($x $y))
  (superpose ((a b) (b c) (c d))))
!(bind! &built (mork:new-space))
!(mork:add-atoms &built ((edge x y) (edge y z)))
!(assertEqual
  (mork:match &built (edge $x $y) ($x $y))
  (superpose ((x y) (y z))))
