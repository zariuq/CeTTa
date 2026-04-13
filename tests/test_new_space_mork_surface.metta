!(import! &self mork)
; Preferred explicit constructor.
!(bind! &m (mork:new-space))
!(assertEqualToResult (get-type &m) (MorkSpace))
!(assertEqualToResult (mork:get-atoms &m) ())
!(assertEqualToResult (mork:match &m (edge $x $y) ($x $y)) ())
!(assertEqualToResult
  (add-atom &m (edge a b))
  ((Error (add-atom &m (edge a b)) "MorkSpace requires explicit mork:add-atom; add-atom does not operate on MorkSpace")))
!(mork:add-atom &m (edge a b))
!(assertEqual (mork:match &m (edge $x $y) ($x $y)) (superpose ((a b))))
!(assertEqualToResult (mork:size &m) (1))

; Legacy generic construction is now quarantined.
!(assertEqualToResult
  (new-space mork)
  ((Error (new-space mork) "generic (new-space mork) is disabled; use (mork:new-space)")))
