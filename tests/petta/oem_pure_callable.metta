(= (pure-callable-loop $remaining $value)
   (if (< $remaining 1) $value
     (let $step
          (|-> ($candidate $fallback)
               (if (is-var $candidate)
                   (cons $candidate $fallback)
                   $fallback))
       (pure-callable-loop (- $remaining 1) ($step () $value)))))

(= (pure-callable-unary $value)
   (let $step (|-> ($item) (+ $item 1)) ($step $value)))

; Effects remain on canonical dynamic dispatch and occur exactly once.
(= (effectful-callable $value)
   (let $step
        (|-> ($item)
             (progn (add-atom &self (pure-callable-effect $item)) $item))
     ($step $value)))

; Under- and over-application retain the callable protocol.
(= (partial-callable-is-expression)
   (let $step (|-> ($left $right) (Pair $left $right))
     (is-expr ($step held))))
(= (over-applied-callable)
   (let $step
        (|-> ($left $right)
             (|-> ($last) (Triple $left $right $last)))
     ($step a b c)))

!(pure-callable-loop 1000 done)
!(pure-callable-unary 4)
!(effectful-callable once)
!(collapse (match &self (pure-callable-effect $value) $value))
!(partial-callable-is-expression)
!(over-applied-callable)
