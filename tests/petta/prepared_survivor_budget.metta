; Transient allocations, genuinely live structure, aliases and an escaping value.
(= (budget-loop $n $sum $held)
   (if (== $n 0) (result $sum $held)
       (budget-loop (- $n 1) (+ $sum $n) $held)))
(= (budget-build $n $tail)
   (if (== $n 0) $tail (budget-build (- $n 1) (Cell $n $tail))))
(= (budget-total Nil) 0)
(= (budget-total (Cell $n $tail)) (+ $n (budget-total $tail)))
(= (budget-live $n)
   (let $held (budget-build $n Nil)
     (let (result $sum (Pair $left $right))
          (budget-loop 20000 0 (Pair $held $held))
       (live $sum (budget-total $left) (budget-total $right)))))
!(budget-live 12000)

(= (budget-open $v)
   (let (result $sum (Pair $left $right))
        (budget-loop 2000 0 (Pair $v $v))
     (let $left 7 (aliases $sum $right))))
!(budget-open $unknown)

; The returned value is an independently usable constructor, not a nursery pointer.
!(let (result $sum $held) (budget-loop 20000 0 (Box 41))
   (let (Box $n) $held (escaped $sum (+ $n 1))))

; Many suspended callers can share a tiny value. Root scanning has a cost
; even when almost none of the arithmetic intermediates survives.
(= (budget-id $value) $value)
(= (budget-deep $n $held)
   (if (== $n 0) $held (budget-id (budget-deep (- $n 1) $held))))
!(let (Box $n) (budget-deep 40000 (Box 42)) (deep-roots $n))
