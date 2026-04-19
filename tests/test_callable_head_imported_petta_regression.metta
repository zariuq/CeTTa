!(import! &self support/petta_callable_heads/lib/a)

(= (myplus (in $X (1 2 3))
           (in $Y (2 3)))
   (in (+ $X $Y) (3 4 5)))

!(assertEqual (collapse (myplus 1 3)) (4))
!(assertEqual (collapse (myplus 3 3)) ())
!(assertEqual (collapse (myplus $x 3)) (4 5))
