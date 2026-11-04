; Correctness-checked workloads over the list library.  Each kernel computes
; an integer its driver knows in closed form, so a timing cannot hide a wrong
; answer.

!(import! &self list)

(= (bench:list-range-desc $n)
   (if (<= $n 0)
       ()
       (let $tail (eval (bench:list-range-desc (- $n 1)))
         (list:cons (- $n 1) $tail))))

(= (bench:list-sum $xs)
   (list:foldl $xs 0 $acc $item (eval (+ $acc $item))))

(= (bench:list-build-sum $n)
   (bench:list-sum (bench:list-range-desc $n)))

(= (bench:list-reverse-sum $n)
   (bench:list-sum
     (list:reverse (bench:list-range-desc $n))))

(= (bench:list-append-sum $n)
   (let* (($xs (bench:list-range-desc $n))
          ($ys (bench:list-range-desc $n)))
     (bench:list-sum (list:append $xs $ys))))

(= (bench:list-map-sum $n)
   (bench:list-sum
     (list:map (bench:list-range-desc $n) $x (+ $x 1))))
