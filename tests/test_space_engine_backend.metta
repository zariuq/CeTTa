!(import! &self mork)
!(bind! &h (new-space hash))
!(space-set-backend! &h pathmap)
!(assertEqualToResult (get-type &h) ((Space pathmap)))
!(add-atom &h (edge a b))
!(assertEqualToResult (match &h (edge $x $y) ($x $y)) ((a b)))
!(assertEqualToResult
  (step! &h 1)
  ((Error (step! &h 1) NoStepSemantics)))
!(assertEqualToResult
  (space-set-backend! &h mork)
  ((Error (space-set-backend! &h mork) "generic space-set-backend! no longer accepts mork; use (mork:new-space)")))
!(space-set-backend! &h native-candidate-exact)
!(assertEqualToResult (get-type &h) ((Space hash)))
!(assertEqualToResult (size &h) (1))
!(assertEqualToResult
  (space-set-backend! &h nope-backend)
  ((Error (space-set-backend! &h nope-backend) UnknownSpaceEngine)))
