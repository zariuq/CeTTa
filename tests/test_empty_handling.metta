; Empty handling — Empty removed from results, not returned to user
; Ref: metta.md lines 127-128, minimal-metta.md lines 57-59

; case with no matching branch returns empty (no results)
!(assertEqualToResult (case 99 ((1 one) (2 two))) ())

; superpose of empty list returns no results
!(assertEqualToResult (superpose ()) ())

; match with no matches returns no results
!(assertEqualToResult (match &self (nonexistent-pattern) found) ())
