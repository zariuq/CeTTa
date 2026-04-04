!(import! &self mork)

!(bind! &plain (new-space pathmap))
!(assertEqualToResult
  (get-type &plain)
  ((Space pathmap)))
!(include &plain mm2_exec_basic.mm2)
!(assertEqualToResult
  (get-type &plain)
  ((Space pathmap)))
!(assertEqualToResult
  (size &plain)
  (3))
!(assertEqualToResult
  (step! &plain 5)
  ((Error (step! &plain 5) NoStepSemantics)))
!(assertEqualToResult
  (match &plain (mm2_exec $p $src $snk) exec-present)
  (exec-present))

!(bind! &ws (mork:new-space))
!(assertEqualToResult
  (get-type &ws)
  (MorkSpace))
!(mork:include! &ws mm2_exec_basic.mm2)
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
