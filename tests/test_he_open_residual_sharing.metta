; Unresolved variables in an equation result keep their sharing: one
; caller variable used twice stays one variable, and duplicate equation
; rows give duplicate results. The expected output agrees with Hyperon
; Experimental 0.2.10.
(= (dup-pair $x) (pair $x $x))
(= (nest-pair $x) (dup-pair $x))
!(dup-pair $a)
!(nest-pair $a)
!(dup-pair (pair $a $b))
!(dup-pair (pair $a $a))

(= (dup-twice $x) (Pair $x $x))
(= (dup-twice $x) (Pair $x $x))
(= (wrap $x $tail) (Combined $x $tail))
!(wrap (dup-twice $a) $a)
!(wrap (dup-twice (Node $a $b)) (Node $a $b))
!(wrap (dup-twice (Node $a $a)) (Node $a $a))
