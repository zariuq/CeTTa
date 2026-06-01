; Canonical rationals must round-trip through space storage and matching.

!(bind! &ratios (new-space))
!(add-atom &ratios (ratio 2/4))

!(assertEqualToResult
  (match &ratios (ratio $x) $x)
  (1/2))

!(assertEqualToResult
  (match &ratios (ratio 1/2) ok)
  (ok))

!(assertEqualToResult
  (match &ratios (ratio 4/2) ok)
  ())
