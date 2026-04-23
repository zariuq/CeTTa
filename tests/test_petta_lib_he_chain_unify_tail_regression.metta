!(import! &self ../../hyperon/PeTTa/lib/lib_he)

(= (div-small $x $y $accum)
   (chain (eval (- $x $y)) $r1
     (chain (eval (< $r1 0)) $r2
       (chain (unify $r2 True
         $accum
         (chain (eval (+ 1 $accum)) $inc
           (chain (eval (div-small $r1 $y $inc)) $r4 $r4)
         )) $r3 $r3
       )
     )
   )
)

!(test (chain (eval (div-small 500 5 0)) $rr $rr) 100)
