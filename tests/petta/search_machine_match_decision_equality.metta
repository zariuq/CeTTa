; Repeated variables in authored heads are equality constraints.  A ground
; disagreement may reject only the nonlinear occurrences; agreement preserves
; source order and duplicate occurrences for the canonical matcher.

(= (equal-choice zero $x $x) zero)
(= (equal-choice one $x $y) one)
(= (equal-choice two $x $x) two)

!(collapse (equal-choice $tag left right))
!(collapse (equal-choice $tag same same))
