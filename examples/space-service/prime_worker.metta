; One captured batch enters as data. Ordinary evaluation schedules its work;
; the caller publishes the completed values against that snapshot's revision.
(= (work:sum $n $acc)
   (if (== $n 0) $acc (work:sum (- $n 1) (+ $acc $n))))

(= (work:transform $inputs)
   (collapse
      (let $n (superpose $inputs)
         (Computed $n (work:sum $n 0)))))
