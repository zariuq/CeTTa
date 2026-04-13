!(import! &self mork)
; Preferred explicit constructor.
!(bind! &m (mork:new-space))
!(assertEqualToResult (get-type &m) (MorkSpace))
!(assertEqualToResult
  (add-atom &m (edge a b))
  ((Error (add-atom &m (edge a b)) "MorkSpace requires explicit mork:add-atom; add-atom does not operate on MorkSpace")))
!(mork:add-atom &m (edge a b))
!(assertEqual (mork:match &m (edge $x $y) ($x $y)) (superpose ((a b))))
!(assertEqualToResult (collapse (mork:match &m (edge x y) hit)) (()))
!(assertEqualToResult (mork:size &m) (1))
!(bind! &bulk (mork:new-space))
!(mork:add-atoms &bulk (collapse (superpose ((edge c d) (edge e f) (edge g h)))))
!(assertEqualToResult (mork:size &bulk) (3))
!(assertEqual (mork:match &bulk (edge $x $y) ($x $y))
  (superpose ((c d) (e f) (g h))))

; Legacy generic construction is now quarantined.
!(assertEqualToResult
  (new-space mork)
  ((Error (new-space mork) "generic (new-space mork) is disabled; use (mork:new-space)")))
