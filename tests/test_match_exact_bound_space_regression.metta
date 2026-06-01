!(bind! &s (new-space))
!(add-atom &s (foo a))

!(assertEqualToResult
  (match &s (foo a) ok)
  (ok))

!(assertEqualToResult
  (match &s (foo $x) ok)
  (ok))
