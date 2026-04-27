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
!(assertEqualToResult (collapse (mork:match &m (edge x y) hit)) (()))
!(assertEqualToResult (mork:size &m) (1))
!(bind! &bulk (mork:new-space))
!(mork:add-atoms &bulk (collapse (superpose ((edge c d) (edge e f) (edge g h)))))
!(assertEqualToResult (mork:size &bulk) (3))
!(assertEqual (mork:match &bulk (edge $x $y) ($x $y))
  (superpose ((c d) (e f) (g h))))
!(bind! &bulk-direct (mork:new-space))
!(__cetta_lib_mork_space_add_stream &bulk-direct
  (superpose ((edge i j) (edge j k) (edge k l))))
!(assertEqualToResult (mork:size &bulk-direct) (3))
!(assertEqual (mork:match &bulk-direct (edge $x $y) ($x $y))
  (superpose ((i j) (j k) (k l))))
!(add-atom-nodup nope (edge bad arg))
!(assertEqualToResult (select (mork:match &bulk (edge c d) hit)) (hit))
!(assertEqual (once (mork:match &bulk (edge e f) hit))
  (select (mork:match &bulk (edge e f) hit)))
!(assertEqual (collect (mork:match &bulk (edge g h) hit)) (hit))
!(assertEqual (select 0 (mork:match &bulk (edge $x $y) ($x $y))) ())
!(bind! &join (mork:new-space))
!(mork:add-atoms &join (collapse (superpose ((edge a b) (edge b c) (edge a d) (edge d e)))))
!(assertEqualToResult
  (select (mork:match &join (, (edge a $y) (edge $y c)) $y))
  (b))
!(assertEqual
  (once (mork:match &join (, (edge a $y) (edge $y c)) $y))
  (select (mork:match &join (, (edge a $y) (edge $y c)) $y)))
!(assertEqual
  (collect (mork:match &join (, (edge a $y) (edge $y c)) $y))
  (b))

; Legacy generic construction is now quarantined.
!(assertEqualToResult
  (new-space mork)
  ((Error (new-space mork) "generic (new-space mork) is disabled; use (mork:new-space)")))
