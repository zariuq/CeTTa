; A completed sequence retains its open coordinates. Scalar observers need
; its logical shape rather than a substituted copy of every element.
(= (scoped-items) (Data 1 $held0 $held1))
(= (scoped-repeat $n $items $acc)
   (if (> $n 0)
       (scoped-repeat (- $n 1) $items (+ $acc (size-atom $items)))
       $acc))
!(scoped-repeat 64 (scoped-items) 0)
