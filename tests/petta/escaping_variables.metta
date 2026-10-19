; Variables that escape into answers: one fresh variable per activation,
; shared where the program shares it, as SWI-PeTTa prints them, on every
; route and with collections at every few kilobytes (identities reused).
(= (mk $n) (pair $n $x))
(= (twin) (pair $x $x))
(= (wrap $y) (box $y $z))
(= (map-flat $f ()) ())
(= (map-flat $f (cons $h $t)) (cons ($f $h) (map-flat $f $t)))
(= (opens $n) (if (== $n 0) () (cons (pair $n $x) (opens (- $n 1)))))
(= (shared $n) (let $v (mk $n) (pair $v $v)))
!(mk 1)
!(twin)
!(map-flat wrap (cons 1 (cons 2 (cons 3 ()))))
!(opens 4)
!(shared 7)
!(collapse (superpose ((pair 1 $w) (pair 2 $w))))
!(let $l (opens 3) (pair $l $l))
