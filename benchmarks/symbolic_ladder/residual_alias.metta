; A repeated variable is a residual equality. 1 does not alias 2.
; The open query still binds, and the following ground query still answers.

(= (alias $x $x) yes)

!(alias 1 2)
!(alias $y a)
!(alias 1 1)
