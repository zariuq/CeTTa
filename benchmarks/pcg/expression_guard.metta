(: pcg-expression (-> Expression Atom))
(= (pcg-expression $x) OK)
(= (pcg-count $n $acc)
   (if (== $n 0) $acc
       (let $seen (pcg-expression (box $n))
            (pcg-count (- $n 1) (+ $acc 1)))))
!(pcg-count 100000 0)
