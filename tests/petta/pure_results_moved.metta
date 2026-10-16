; A pure call's result leaves the storage that dies with the call by moving
; (each atom records its copy in place) rather than through a copy table:
; its sharing, its variables and its nesting are kept, under frequent
; collections as well (SWI-PeTTa's answers).
(= (range $n) (if (== $n 0) () (cons $n (range (- $n 1)))))
(= (map-flat $f ()) ())
(= (map-flat $f (cons $x $xs)) (cons ($f $x) (map-flat $f $xs)))
(= (twice $x) (pair $x $x))
(= (wrap $v) (box $v (box $v)))
(= (deep $n) (if (== $n 0) leaf (node (deep (- $n 1)) (deep (- $n 1)))))
(= (count-leaves leaf) 1)
(= (count-leaves (node $l $r)) (+ (count-leaves $l) (count-leaves $r)))
!(length (map-flat (+ 1) (range 20000)))
!(twice (range 3))
!(let (pair $a $b) (twice (range 300)) (== $a $b))
!(let $r (wrap $q) (let (box $u (box $w)) $r (== $u $w)))
!(map-flat (+ 1) (range 5))
!(count-leaves (deep 12))
!(let $t (deep 3) (== $t (deep 3)))
