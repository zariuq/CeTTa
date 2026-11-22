!(import! &self ../../lib/petta/lib_he)
(= (constructor-loop $n $total)
   (if (== $n 0) $total
       (unify (Pair $n $total) (Pair $left $right)
          (constructor-loop (- $left 1) (+ $right 1)) -1)))
!(constructor-loop 1000 0)
