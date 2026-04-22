; Regression: let True (> (myplus $x 2) 3) must export concrete witnesses.

(= (in $x $L)
   (let True (is-member $x $L) $x))

(= (myplus (in $X (1 2 3))
           (in $Y (2 3)))
   (in (+ $X $Y) (3 4 5)))

!(assertEqual (collapse (let True (> (myplus $x 2) 3) $x)) (2 3))
