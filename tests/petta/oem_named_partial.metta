(= (partial-sum $left $right) (+ $left $right))
(= (partial-pair $left $right) (Pair $left $right))

(= (partial-loop $remaining $value)
   (if (< $remaining 1) $value
     (let $step (partial-sum 1)
       (partial-loop (- $remaining 1) ($step $value)))))

!(partial-loop 1000 0)
!(let $step (partial-pair (held call)) ($step result))
; A lambda-based partial retains the canonical callable path.
!(let $step ((|-> ($left $right) (Pair $left $right)) held)
   ($step result))
