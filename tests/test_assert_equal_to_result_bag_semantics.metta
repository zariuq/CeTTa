; Result comparison ignores enumeration order but preserves multiplicity.
!(assertEqualToResult
  (superpose (second first second))
  (first second second))

; A duplicate is still a distinct occurrence.
!(assertEqualToResultMsg
  (superpose (same same))
  (same)
  "multiplicity mismatch")
