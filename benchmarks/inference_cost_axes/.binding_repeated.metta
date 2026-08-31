(= (binding-id $x) $x)
(= (binding-box $x) (Box $x))
(= (binding-pair $x) (Pair $x $x))
(= (binding-region-walk $n)
   (if (== $n 0)
       done
       (let* (((Pair $next $next) (binding-pair (- $n 1))))
                (binding-region-walk $next))))
!(binding-region-walk 50)
