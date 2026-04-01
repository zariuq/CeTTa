!(import! &ws mm2_exec_basic.mm2)
!(assertEqualToResult
  (get-type &ws)
  ((Space mork)))
!(assertEqualToResult
  (size &ws)
  (3))
!(assertEqualToResult
  (step! &ws 10)
  (1))
!(assertEqual
  (match &ws (path $x $y) ($x $y))
  (superpose ((a c))))
!(bind! &mods (module-inventory!))
!(assertEqualToResult
  (match &mods
    (loaded-module-format "mm2_exec_basic.mm2" mm2 none)
    ok)
  (ok))
!(assertEqualToResult
  (match &mods
    (loaded-module-storage "mm2_exec_basic.mm2" materialized)
    ok)
  (ok))
