; A ground query must still use bidirectional matching when the stored side
; contains variables.
(= (eq $x $x) T)

!(assertEqualToResult
  (match &self (= (eq red red) T) hit)
  (hit))

!(assertEqualToResult
  (match &self (= (eq red blue) T) hit)
  ())
