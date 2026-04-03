; Prototype list-benchmark helpers for CeTTa.
; These are intentionally small and direct so we can compare the hot list lanes:
; - fast lists
; - explicit clists

!(import! &self list_proto)

(= (bench:list-range-desc $n)
   (if (<= $n 0)
       ()
       (let $tail (eval (bench:list-range-desc (- $n 1)))
         (list:cons (- $n 1) $tail))))

(= (bench:clist-range-desc $n)
   (if (<= $n 0)
       (clist:nil)
       (let $tail (eval (bench:clist-range-desc (- $n 1)))
         (clist:cons (- $n 1) $tail))))

(= (bench:list-sum $xs)
   (list:foldl $xs 0 $acc $item (eval (+ $acc $item))))

(= (bench:clist-sum $xs)
   (clist:foldl $xs 0 $acc $item (eval (+ $acc $item))))

(= (bench:list-build-sum $n)
   (bench:list-sum (bench:list-range-desc $n)))

(= (bench:clist-build-sum $n)
   (bench:clist-sum (bench:clist-range-desc $n)))

(= (bench:list-reverse-sum $n)
   (bench:list-sum
     (list:reverse (bench:list-range-desc $n))))

(= (bench:clist-reverse-sum $n)
   (bench:clist-sum
     (clist:reverse (bench:clist-range-desc $n))))

(= (bench:list-append-sum $n)
   (let* (($xs (bench:list-range-desc $n))
          ($ys (bench:list-range-desc $n)))
     (bench:list-sum (list:append $xs $ys))))

(= (bench:clist-append-sum $n)
   (let* (($xs (bench:clist-range-desc $n))
          ($ys (bench:clist-range-desc $n)))
     (bench:clist-sum (clist:append $xs $ys))))

(= (bench:list-map-sum $n)
   (bench:list-sum
     (list:map (bench:list-range-desc $n) $x (+ $x 1))))

(= (bench:clist-map-sum $n)
   (bench:clist-sum
     (clist:map (bench:clist-range-desc $n) $x (+ $x 1))))
