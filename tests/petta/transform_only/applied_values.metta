; Values a call can apply: a function's name, a partial application, a lambda.
(= (each $f ()) ())
(= (each $f (cons $x $xs)) (cons ($f $x) (each $f $xs)))
!(test (each (+ 1) (1 2 3)) (2 3 4))

(= (same $g) $g)
(= (twice $r $g) ($r ($r $g)))
!(test (twice same item) item)

(= (with-two $f) ($f 43 44))
!(test (let* (($k 45) ($f (|-> ($x $y) (42 $x $y $k)))) (with-two $f)) (42 43 44 45))

(= (is-one $a) (== $a 1))
!(test (each (== 1) (1 2 1)) (each is-one (1 2 1)))
