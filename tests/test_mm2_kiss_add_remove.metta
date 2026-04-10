!(import! &self mork)

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (task milk))
!(mork:add-atom &ws (trigger))
!(mork:add-atom &ws
  (exec (0 complete)
    (, (trigger)
       (task $name))
    (O (+ (done $name))
       (- (task $name))
       (- (trigger)))))
!(assertEqualToResult
  (mork:step! &ws)
  (1))
!(assertEqual
  (collapse (mork:get-atoms &ws))
  ((done milk)))
!(assertEqualToResult
  (mork:step! &ws)
  (0))
