!(bind! &s (new-space))
!(add-atom &s (foo $x))

!(assertEqualToResult
  (match &s (foo a) ok)
  (ok))

!(assertEqualToResult
  (with-space-snapshot $snap &s
    (match $snap (foo a) ok))
  (ok))
