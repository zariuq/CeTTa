!(import! &ws mm2_exec_basic.mm2)
!(assertEqualToResult
  (get-type &ws)
  ((Space atom)))
!(assertEqualToResult
  (size &ws)
  (3))
!(assertEqualToResult
  (step! &ws 10)
  ((Error (step! &ws 10) NoStepSemantics)))
!(assertEqualToResult
  (match &ws (mm2_exec $priority $src $sink) stored)
  (stored))
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
