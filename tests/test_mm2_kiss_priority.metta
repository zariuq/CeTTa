!(bind! &ws (new-space mork))
!(add-atom &ws (ready))
!(add-atom &ws
  (exec (1 second)
    (, (ready))
    (O (+ (ran second))
       (- (ready)))))
!(add-atom &ws
  (exec (0 first)
    (, (ready))
    (O (+ (ran first))
       (- (ready)))))
!(assertEqualToResult
  (step! &ws)
  (1))
!(assertEqualToResult
  (match &ws (ran $who) $who)
  (first))
