!(import! &self mork)

!(bind! &h (new-space hash))
!(assertEqualToResult
  (step! &h 1)
  ((Error (step! &h 1) NoStepSemantics)))

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (trigger))
!(mork:add-atom &ws
  (exec (0 hit)
    (, (trigger))
    (O (+ (done))
       (- (trigger)))))
!(assertEqualToResult
  (mork:step! &ws 1)
  (1))
!(assertEqual
  (collapse (mork:get-atoms &ws))
  ((done)))
!(assertEqualToResult
  (mork:step! &ws 5)
  (0))
!(assertEqual
  (mork:step! &ws "bad")
  (superpose
    ((Error (mork:step! &ws "bad") IncorrectNumberOfArguments)
     (Error (mork:step! &ws "bad") (BadArgType 2 Number String)))))
