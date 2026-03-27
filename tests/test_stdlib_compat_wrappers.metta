; HE-style stdlib wrappers over existing CeTTa primitives.

!(assertEqual (empty) Empty)

!(assertEqualToResult
   (unique (superpose (a b a c b)))
   (a b c))

!(assertEqualToResult
   (union (superpose (a b b c)) (superpose (b c c d)))
   (a b b c b c c d))

!(assertEqualToResult
   (intersection (superpose (a b c c)) (superpose (b c c c d)))
   (b c c))

!(assertEqualToResult
   (subtraction (superpose (a b b c)) (superpose (b c c d)))
   (a b))

!(assertAlphaEqual (quote ($x $x)) (quote ($y $y)))

!(assertEqualToResult
   (assertAlphaEqualMsg (quote ($x $x)) (quote ($y $y)) "alpha-pass")
   (()))

!(assertEqualToResult
   (assertAlphaEqualToResultMsg (quote ($x)) ((quote ($x $x))) "alpha mismatch")
   ((Error (assertAlphaEqualToResultMsg (quote ($x)) ((quote ($x $x)))) "alpha mismatch")))
