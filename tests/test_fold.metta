; HE-extended fold: consume nondeterministic results without first packaging
; them into an expression.

(= (nums) (superpose (1 2 3)))

!(fold (nums) 0 $acc $item (eval (+ $acc $item)))
!(fold (search-policy recursive-dependent-proof (order reverse))
       (superpose (a b c))
       ()
       $acc
       $item
       (eval (cons-atom $item $acc)))
!(fold (superpose ()) 5 $acc $item (eval (+ $acc $item)))
!(assertEqual (fold (superpose (1 Empty 2)) 0 $acc $item (eval (+ $acc $item))) 3)
!(fold (superpose (1)) 0 $acc $item (superpose ($acc $item)))
