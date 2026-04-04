!(import! &self mork)

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws
  (exec (0 step)
    (, (edge $x $y)
       (edge $y $z))
    (O (+ (path $x $z)))))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))

!(assertEqualToResult
  (mork:step! &ws)
  (1))
!(assertEqual
  (collapse (mork:get-atoms &ws))
  ((edge a b)
   (edge b c)
   (path a c)))
!(assertEqualToResult
  (mork:step! &ws)
  (0))
