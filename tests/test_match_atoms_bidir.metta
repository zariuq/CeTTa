; match_atoms bidirectional matching
; Ref: metta.md lines 577-617

; Ground atoms — straightforward
(color red)
(color green)
(color blue)
!(assertEqualToResult
  (match &self (color $c) $c)
  (red green blue))

; Nested pattern
(pair (a 1) (b 2))
!(assertEqualToResult
  (match &self (pair (a $x) (b $y)) ($x $y))
  ((1 2)))
