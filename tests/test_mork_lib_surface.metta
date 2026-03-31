!(import! &self mork)
!(bind! &h (new-space mork))
!(assertEqualToResult
  (get-type &h)
  ((Space mork)))
!(assertEqualToResult
  (size &h)
  (0))
!(add-atom &h (edge a b))
!(add-atom &h (edge b c))
!(assertEqualToResult
  (size &h)
  (2))
!(assertEqual
  (match &h (edge $x $y) ($x $y))
  (superpose ((a b) (b c))))
!(assertEqual
  (collapse (get-atoms &h))
  ((edge a b) (edge b c)))
!(bind! &snap (new-space mork))
!(bind! &atoms (collapse (get-atoms &h)))
!(add-atoms &snap &atoms)
!(add-atom &h (edge c d))
!(assertEqual
  (collapse (get-atoms &snap))
  ((edge a b) (edge b c)))
!(assertEqual
  (match &h (edge $x $y) ($x $y))
  (superpose ((a b) (b c) (c d))))
!(bind! &built (new-space mork))
!(add-atoms &built ((edge x y) (edge y z)))
!(assertEqual
  (match &built (edge $x $y) ($x $y))
  (superpose ((x y) (y z))))
