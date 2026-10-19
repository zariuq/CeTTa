; A value a lexical map, fold or filter body captured is read as a value in
; every step: a captured ((+ 1 2)) is that expression, never 3, and a large
; captured value is shared by the steps, not copied or evaluated per item.
(= (mk) (cons-atom (cons-atom + (1 2)) ()))
(= (via-map $big $keys) (map-atom $keys $k (size-atom (car-atom $big))))
(= (via-filter $big $keys) (filter-atom $keys $k (== (size-atom (car-atom $big)) 3)))
(= (via-foldl $big $keys) (foldl-atom $keys 0 $acc $k (+ $acc (size-atom (car-atom $big)))))
!(let $b (mk) (via-map $b (x y)))
!(let $b (mk) (via-filter $b (x y)))
!(let $b (mk) (via-foldl $b (x y)))
!(let $b (mk) (filter-atom (x y z) $k (== (size-atom (car-atom $b)) 2)))
(= (rows $i $n $acc)
   (if (== $i $n) $acc
     (rows (+ $i 1) $n (cons-atom (Row $i (a b c) "text") $acc))))
!(let* (($big (rows 0 2000 ())) ($keys (rows 0 300 ())))
   (size-atom (filter-atom $keys $k (> (size-atom $big) 0))))
!(let* (($big (rows 0 2000 ())) ($keys (rows 0 300 ())))
   (foldl-atom $keys 0 $acc $k (+ $acc (size-atom $big))))
