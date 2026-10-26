; Lists built by cons-atom, then read by grounded operations at every step:
; appended at the end, walked, consed in front, reversed, mapped and joined.
; Every list here is complete when it is built, so none is a chain of cells
; that a reader would flatten.
(= (snoc $xs $x)
   (if (== $xs ()) ($x)
     (let ($h $t) (decons-atom $xs) (cons-atom $h (snoc $t $x)))))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (snoc $acc $n))))
(= (walk $xs)
   (if (== $xs ()) 0 (let ($h $t) (decons-atom $xs) (+ 1 (walk $t)))))
(= (range $n) (if (== $n 0) () (cons-atom $n (range (- $n 1)))))
(= (rev-onto $xs $acc)
   (if (== $xs ()) $acc
     (let ($x $more) (decons-atom $xs) (rev-onto $more (cons-atom $x $acc)))))
!(build 12 ())
!(walk (build 40 ()))
!(size-atom (range 50))
!(rev-onto (range 10) ())
!(let $l (range 30) (walk (map-atom $l $x (+ $x 1))))
!(let $l (range 30) (size-atom (union-atom $l $l)))
