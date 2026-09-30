; A lambda compiles to a clause whose head is its parameters, so a pattern
; parameter is matched as an equation's head pattern is, and the variables
; of the patterns are fresh at every call.
(= (pairs-firsts $l) (maplist (|-> (($a $b)) $a) $l))
!((|-> (($a $b)) $a) (x y))
!((|-> ((f $a)) $a) (f x))
!((|-> ((f $a)) $a) (g x))
!((|-> (a) ok) a)
!((|-> ((cons $h $t)) $t) (1 2 3))
!(let $f (|-> (($i $a) $e) ((+ 1 $i) $a)) ($f (0 x) y))
!((|-> ($p) $p) (x y))
!(pairs-firsts ((1 2) (3 4) (5 6)))
!(foldl (|-> ($acc ($p $q)) (+ $acc (* $p $q))) ((1 2) (3 4)) 0)
!((|-> (($a $a)) same) (x x))
!(collapse ((|-> (($a $a)) same) (x y)))
!(maplist (|-> ((cons $h $_)) $h) ((1 2) (3) (4 5 6)))
