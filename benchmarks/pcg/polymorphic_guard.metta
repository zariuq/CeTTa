(: pcg-identity (-> $type $type))
(= (pcg-identity $x) $x)
(= (pcg-count $n $acc)
   (if (== $n 0) $acc
       (let $seen (pcg-identity $n)
            (pcg-count (- $n 1) (+ $acc 1)))))
!(pcg-count 10000 0)
