!(bind! &ws (new-space mork))
!(add-atom &ws (trigger))
!(add-atom &ws
  (exec (0 (0 (0 0)))
    (, (trigger))
    (O (+ (fired (0 (0 (0 0))))))))
!(add-atom &ws
  (exec (0 (0 (1 0)))
    (, (trigger))
    (O (+ (fired (0 (0 (1 0))))))))
!(add-atom &ws
  (exec (0 (1 0))
    (, (trigger))
    (O (+ (fired (0 (1 0)))))))
!(add-atom &ws
  (exec (1 0)
    (, (trigger))
    (O (+ (fired (1 0))))))
!(assertEqualToResult
  (step! &ws)
  (1))
!(assertEqual
  (match &ws (fired $p) $p)
  (superpose ((0 (0 (0 0)))
              (0 (0 (1 0))))))
!(assertEqualToResult
  (step! &ws 3)
  (2))
!(assertEqual
  (match &ws (fired $p) $p)
  (superpose ((0 (0 (0 0)))
              (0 (0 (1 0)))
              (0 (1 0))
              (1 0))))
