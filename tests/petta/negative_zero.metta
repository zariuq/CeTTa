; 0.0 and -0.0 are two Prolog terms: a literal keeps its sign, == tells them
; apart, and each matches only itself, in an equation head or a space.
; Arithmetic still compares them as equal numbers.
(= (f 0.0) zero)
(= (f -0.0) negative-zero)
(foo 0.0)
(foo -0.0)
!(println! (literals 0.0 -0.0 (* -1.0 0.0)))
!(println! (identity (== 0.0 -0.0) (== -0.0 -0.0) (== 0.0 0.0)))
!(println! (heads (f 0.0) (f -0.0)))
!(println! (space (collapse (match &self (foo -0.0) found))))
!(println! (arithmetic (< -0.0 0.0) (> 0.0 -0.0) (== (+ 0.0 0.0) 0.0)))
