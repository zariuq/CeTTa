!(bind! &ws (new-space))
!(assertEqualToResult
  (get-type &ws)
  ((Space atom)))
!(include &ws mm2_exec_basic.mm2)
!(assertEqualToResult
  (get-type &ws)
  ((Space mork)))
!(assertEqualToResult
  (size &ws)
  (3))
!(assertEqualToResult
  (step! &ws 5)
  (1))
!(assertEqualToResult
  (size &ws)
  (3))
!(assertEqual
  (match &ws (path $x $y) ($x $y))
  (superpose ((a c))))
