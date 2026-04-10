!(import! &self mork)

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (ready))
!(mork:add-atom &ws
  (exec (1 second)
    (, (ready))
    (O (+ (ran second))
       (- (ready)))))
!(mork:add-atom &ws
  (exec (0 first)
    (, (ready))
    (O (+ (ran first))
       (- (ready)))))
!(assertEqualToResult
  (mork:step! &ws)
  (1))
!(assertEqualToResult
  (mork:match &ws (ran $who) $who)
  (first))
