!(import! &self mork)

!(bind! &plain (new-space pathmap))
!(assertEqualToResult
  (get-type &plain)
  ((Space pathmap)))
!(assertEqualToResult
  (size &plain)
  (0))
!(collapse (include &plain mm2_exec_basic.mm2))
!(assertEqualToResult
  (get-type &plain)
  ((Space pathmap)))
!(assertEqualToResult
  (size &plain)
  (0))
!(assertEqualToResult
  (match &plain (mm2_exec $p $src $snk) exec-present)
  ())

!(bind! &ws (mork:new-space))
!(assertEqualToResult
  (get-type &ws)
  (MorkSpace))
!(pragma! mork-space-sugar allow)
!(include &ws mm2_exec_basic.mm2)
!(assertEqualToResult
  (get-type &ws)
  (MorkSpace))
!(assertEqualToResult
  (mork:size &ws)
  (3))
!(assertEqualToResult
  (mork:step! &ws 5)
  (1))
!(assertEqualToResult
  (mork:size &ws)
  (3))
!(assertEqual
  (mork:match &ws (path $x $y) ($x $y))
  (superpose ((a c))))
