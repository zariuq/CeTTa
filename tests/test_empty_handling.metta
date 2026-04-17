; Empty handling — Empty removed from results, not returned to user
; Ref: metta.md lines 127-128, minimal-metta.md lines 57-59

; case with no matching branch returns empty (no results)
!(assertEqualToResult (case 99 ((1 one) (2 two))) ())

; superpose of empty list returns no results
!(assertEqualToResult (superpose ()) ())

; match with no matches returns no results
!(assertEqualToResult (match &self (nonexistent-pattern) found) ())

; collapse also filters the internal Empty sentinel rather than packaging it
; into the resulting tuple.
!(assertEqual (collapse Empty) ())

; Empty produced via the stdlib wrapper must be filtered the same way.
!(assertEqual
  (collapse (let $x (superpose (a b)) (if (== $x a) (empty) $x)))
  (b))
