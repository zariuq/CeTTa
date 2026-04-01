!(bind! &s (new-space))
!(add-atom &s (= (foo a) done))
!(assertEqualToResult
  (with-space-snapshot $snap &s (collapse (evalc (foo a) $snap)))
  ((done)))
!(assertEqualToResult
  (collapse (evalc (foo a) &s))
  ((done)))
