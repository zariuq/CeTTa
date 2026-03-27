; HE leaves malformed with-space-snapshot calls unreduced rather than surfacing
; an eager error.
!(bind! &snapbox (new-space))
!(assertEqualToResult
  (with-space-snapshot &x &y)
  ((with-space-snapshot &x &y)))
!(assertEqualToResult
  (with-space-snapshot &x &y &z &w)
  ((with-space-snapshot &x &y &z &w)))
!(assertEqualToResult
  (with-space-snapshot $snap 1 $snap)
  ((with-space-snapshot $snap 1 $snap)))
