!(bind! &ws (new-space mork))
!(add-atom &ws
  (exec (0 step)
    (, (edge $x $y)
       (edge $y $z))
    (O (+ (path $x $z)))))
!(add-atom &ws (edge a b))
!(add-atom &ws (edge b c))

!(assertEqualToResult
  (step! &ws)
  (1))
!(assertEqual
  (collapse (get-atoms &ws))
  ((edge a b)
   (edge b c)
   (path a c)))
!(assertEqualToResult
  (step! &ws)
  (0))
