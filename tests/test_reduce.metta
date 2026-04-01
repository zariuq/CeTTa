; Compatibility alias coverage: reduce remains supported, but fold is now the
; canonical public name for this nondeterministic-results fold.

(= (nums) (superpose (1 2 3)))

!(reduce (nums) 0 $acc $item (eval (+ $acc $item)))
!(reduce (search-policy recursive-dependent-proof (order reverse))
         (superpose (a b c))
         ()
         $acc
         $item
         (eval (cons-atom $item $acc)))
!(reduce (superpose ()) 5 $acc $item (eval (+ $acc $item)))
!(reduce (superpose (1)) 0 $acc $item (superpose ($acc $item)))
