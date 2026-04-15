!(register-module! .)
!(import! &self mork)
!(bind! &ws (mork:new-space))
!(assertEqualToResult
  (get-type &ws)
  (MorkSpace))
!(assertEqualToResult
  (add-atom &ws (edge a b))
  ((Error (add-atom &ws (edge a b)) "MorkSpace requires explicit mork:add-atom; add-atom does not operate on MorkSpace")))
!(pragma! mork-space-sugar allow)
!(include &ws mm2_exec_basic.mm2)
!(assertEqualToResult
  (size &ws)
  (3))
!(assertEqualToResult
  (space-len &ws)
  (3))
!(assertEqualToResult
  (step! &ws 5)
  (1))
!(assertEqual
  (match &ws (path $x $y) ($x $y))
  (superpose ((a c))))
