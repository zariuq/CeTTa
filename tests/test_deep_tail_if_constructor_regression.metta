; Tail-recursive if must not consume C stack, even when the carried value
; grows as constructor-shaped data.

(= (expandK $expression $n)
   (if (== $n 0)
       done
       (let $tmp (add-atom &self (num $expression))
         (expandK (S $expression) (- $n 1)))))

!(assertEqual (expandK Z 600) done)
!(assertEqual
  (size (collapse (match &self (num $x) $x)))
  600)
