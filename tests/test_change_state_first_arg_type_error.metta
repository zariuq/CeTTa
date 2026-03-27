; HE reports a non-state first argument to change-state! as BadArgType.
!(assertAlphaEqualToResult
  (change-state! 1 2)
  ((Error (change-state! 1 2) (BadArgType 1 (StateMonad $t) Number))))
