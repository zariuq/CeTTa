; Prototype list-benchmark helpers for CeTTa.
; These are intentionally small and direct so we can compare the hot list lanes:
; - expression-backed sequences
; - explicit cons-lists

!(import! &self list_proto)

(= (bench:expr-range-desc $n)
   (if (<= $n 0)
       ()
       (let $tail (eval (bench:expr-range-desc (- $n 1)))
         (expr-cons (- $n 1) $tail))))

(= (bench:cons-range-desc $n)
   (if (<= $n 0)
       (conslist:nil)
       (let $tail (eval (bench:cons-range-desc (- $n 1)))
         (conslist:cons (- $n 1) $tail))))

(= (bench:expr-sum $xs)
   (expr-foldl $xs 0 $acc $item (eval (+ $acc $item))))

(= (bench:cons-sum $xs)
   (conslist:foldl $xs 0 $acc $item (eval (+ $acc $item))))

(= (bench:expr-build-sum $n)
   (bench:expr-sum (bench:expr-range-desc $n)))

(= (bench:cons-build-sum $n)
   (bench:cons-sum (bench:cons-range-desc $n)))

(= (bench:expr-reverse-sum $n)
   (bench:expr-sum
     (expr-reverse (bench:expr-range-desc $n))))

(= (bench:cons-reverse-sum $n)
   (bench:cons-sum
     (conslist:reverse (bench:cons-range-desc $n))))

(= (bench:expr-append-sum $n)
   (let* (($xs (bench:expr-range-desc $n))
          ($ys (bench:expr-range-desc $n)))
     (bench:expr-sum (expr-append $xs $ys))))

(= (bench:cons-append-sum $n)
   (let* (($xs (bench:cons-range-desc $n))
          ($ys (bench:cons-range-desc $n)))
     (bench:cons-sum (conslist:append $xs $ys))))

(= (bench:expr-map-sum $n)
   (bench:expr-sum
     (expr-map (bench:expr-range-desc $n) $x (+ $x 1))))

(= (bench:cons-map-sum $n)
   (bench:cons-sum
     (conslist:map (bench:cons-range-desc $n) $x (+ $x 1))))
