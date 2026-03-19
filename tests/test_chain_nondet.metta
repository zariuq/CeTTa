; chain with nondeterministic first arg
; Ref: minimal-metta.md lines 103-116
(= (pick) a)
(= (pick) b)

; chain should produce one template per result
!(assertEqualToResult
  (chain (pick) $x (got $x))
  ((got a) (got b)))
