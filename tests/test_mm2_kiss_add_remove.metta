!(bind! &ws (new-space mork))
!(add-atom &ws (task milk))
!(add-atom &ws (trigger))
!(add-atom &ws
  (exec (0 complete)
    (, (trigger)
       (task $name))
    (O (+ (done $name))
       (- (task $name))
       (- (trigger)))))
!(assertEqualToResult
  (step! &ws)
  (1))
!(assertEqual
  (collapse (get-atoms &ws))
  ((done milk)))
!(assertEqualToResult
  (step! &ws)
  (0))
