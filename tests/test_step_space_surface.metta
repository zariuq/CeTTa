!(bind! &h (new-space hash))
!(assertEqualToResult
  (step! &h 1)
  ((Error (step! &h 1) NoStepSemantics)))

!(bind! &ws (new-space mork))
!(add-atom &ws (trigger))
!(add-atom &ws
  (exec (0 hit)
    (, (trigger))
    (O (+ (done))
       (- (trigger)))))
!(assertEqualToResult
  (step! &ws 1)
  (1))
!(assertEqual
  (collapse (get-atoms &ws))
  ((done)))
!(assertEqualToResult
  (step! &ws 5)
  (0))
!(assertEqualToResult
  (step! &ws "bad")
  ((Error (step! &ws "bad") ExpectedNonNegativeStepCount)))
