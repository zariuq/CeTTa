; Scientific float tokens are values at both sides of the PathMap bridge.
; A symbol containing an exponent marker but no number stays a symbol.

!(bind! &numbers (new-space pathmap))
!(add-atom &numbers (sample tiny 1.0e-06))
!(add-atom &numbers (sample label modelE))

!(assertEqualToResult
  (match &numbers (sample tiny $value) (get-metatype $value))
  (Grounded))
!(assertEqualToResult
  (match &numbers (sample tiny $value) (get-type $value))
  (Number))
!(assertEqualToResult
  (match &numbers (sample tiny $value) (=alpha $value 1.0e-06))
  (True))
!(assertEqualToResult
  (match &numbers (sample label $value) (get-metatype $value))
  (Symbol))
