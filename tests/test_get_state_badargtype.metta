; HE reports non-state get-state failures as BadArgType against StateMonad.
!(assertAlphaEqualToResult
  (get-state 1)
  ((Error (get-state 1) (BadArgType 1 (StateMonad $t) Number))))
