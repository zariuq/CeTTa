(= (sealed-loop $remaining $value)
   (if (< $remaining 1) $value
     (let $next (sealed () (+ $value 1))
       (sealed-loop (- $remaining 1) $next))))

(= (sealed-private)
   (sealed ($private) (progn (= $private private) $private)))

(= (sealed-effect $value)
   (sealed () (progn (println! sealed-effect) $value)))

!(sealed-loop 1000 0)
!(sealed-private)
!(sealed-effect kept)
; Wrong arity remains ordinary data.
!(let $value (sealed a) (is-expr $value))
