; A sequential binding has the same translated binder as ordinary let.
(= (head-count $xs)
   (let* (($items $xs) ((cons $first $rest) $items))
      (+ $first (size-atom $rest))))
!(head-count (1 2 3))
!(collapse (head-count (superpose ((1 2 3) (4)))))
!(collapse (head-count ()))
!(let* (((cons $first $rest) (8 9))) ($first $rest))
!(let* (((quote (cons $head $tail)) (quote (cons a b)))) ($head $tail))
!(collapse (let* (((quote (cons $head $tail)) (1 2))) must-not-match))

; An authored exact relation owns its name, including optional extensions.
(: collect (-> Number Number))
(= (collect $x) (+ $x 1))
!(collect 4)
(: select (-> Number Number))
(= (select $x) (+ $x 2))
!(select 4)
