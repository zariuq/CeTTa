!(import! &self mork)
!(bind! &prog (mork:new-space hash))
!(assertEqualToResult
  (get-type &prog)
  (MorkSpace))
!(mork:add-atom &prog
  (exec (0 demo)
    (, (BTM (edge $x $y))
       (== $x $x))
    (O (+ (path $x $y))
       (- (edge $x $y)))))
!(assertEqualToResult
  (mork:match &prog
    (exec (0 demo)
      (, (BTM (edge $x $y))
         (== $x $x))
      (O (+ (path $x $y))
         (- (edge $x $y))))
    stored)
  (stored))
