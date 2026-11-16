(= (pickc (Cons $x $xs)) $x)
(= (pickc (Cons $x (Cons $y $ys))) (pickc (Cons $y $ys)))
(= (upto $n) (if (== $n 0) Nil (Cons $n (upto (- $n 1)))))
(= (safe $c $placed $dist)
   (if (== $placed Nil) True
       (let (Cons $h $t) $placed
         (if (== $c $h) False
             (if (== (- $c $h) $dist) False
                 (if (== (- $h $c) $dist) False
                     (safe $c $t (+ $dist 1))))))))
(= (place $n $cols $placed)
   (if (== $n 0) $placed
       (let $c (pickc $cols)
         (if (safe $c $placed 1)
             (place (- $n 1) $cols (Cons $c $placed))
             (empty)))))
!(size-atom (collapse (place 8 (upto 8) Nil)))
