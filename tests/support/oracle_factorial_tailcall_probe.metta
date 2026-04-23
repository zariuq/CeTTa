(= (tailcall-const $n)
   (if (== $n 0)
       ok
       (tailcall-const 0)))

(= (tailcall-decr $n)
   (if (== $n 0)
       ok
       (tailcall-decr (- $n 1))))

!(tailcall-const 1)
!(tailcall-decr 1)
