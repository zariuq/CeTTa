(= (f $a $b) (+ $a $b))

!(f 1)
!(repr (f 1))
!(repr (|-> ($x) (+ $x 1)))
!(repr ((|-> ($x) (|-> ($y) (+ $x $y))) 2))
!(let $k 45 (repr (|-> ($x) (+ $x $k))))
!((f 1) 2)

(= (mp) (+))
!(mp 1 1)

!(map-atom (1 2 3) (+ 1))
!(maplist (|-> ($a) (+ 1 $a)) (1 2 3))

(= (g 1) 2)
(= (g 2) 3)

!(collapse ((|-> ($z) (g $z)) $x))
!(collapse ((|-> ($z) (* 2 (g $z))) $x))

!(forall ((|-> ($z) (g $z)) $x)
         (|-> ($v) (< $v 20)))
!(forall ((|-> ($z) (g $z)) $x)
         (|-> ($v) (< $v 3)))
(= (keep-big $x) (> $x 3))
!(filter-atom (1 2 3 4 5) keep-big)
