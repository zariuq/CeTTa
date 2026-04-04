!(import! &self mork)

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (trigger))
!(mork:add-atom &ws
  (exec (0 (0 (0 0)))
    (, (trigger))
    (O (+ (fired (0 (0 (0 0))))))))
!(mork:add-atom &ws
  (exec (0 (0 (1 0)))
    (, (trigger))
    (O (+ (fired (0 (0 (1 0))))))))
!(mork:add-atom &ws
  (exec (0 (1 0))
    (, (trigger))
    (O (+ (fired (0 (1 0)))))))
!(mork:add-atom &ws
  (exec (1 0)
    (, (trigger))
    (O (+ (fired (1 0))))))
!(assertEqualToResult
  (mork:step! &ws)
  (1))
!(assertEqual
  (mork:match &ws (fired $p) $p)
  (superpose ((0 (0 (0 0)))
              (0 (0 (1 0))))))
!(assertEqualToResult
  (mork:step! &ws 3)
  (2))
!(assertEqual
  (mork:match &ws (fired $p) $p)
  (superpose ((0 (0 (0 0)))
              (0 (0 (1 0)))
              (0 (1 0))
              (1 0))))
