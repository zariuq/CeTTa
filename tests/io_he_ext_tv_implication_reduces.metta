; Minimal semantic witness extracted from tests/he_c3_pln_stv.metta.
(= (s-tv (stv $s $c)) $s)
(= (c-tv (stv $s $c)) $c)
(= (TV $x) (match &self (.tv $x $stv) $stv))
(= (TV $x)
   (match &self
     (.tv (Implication $y $x) (stv $s $c))
     (stv (* $s (s-tv (TV $y)))
          (* $c (c-tv (TV $y))))))
(.tv aa (stv 0.5 0.8))
(.tv (Implication aa ff) (stv 0.8 1.0))
!(TV ff)
