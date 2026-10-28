; Prime call-by-need must preserve syntax until the selected equation demands
; a value.  A wildcard can return its argument beneath quote without forcing
; a pure arithmetic expression first.
(= (need-quote $x) (quote $x))
!(need-quote (+ 1 2))

; Syntax preservation is recursive through an ordinary constructor.
(= (need-quote-box $x) (quote (box $x)))
!(need-quote-box (* (+ 1 2) 4))

; Conversely, a value-demanding body still evaluates the argument.
(= (need-add-one $x) (+ $x 1))
!(need-add-one (+ 1 2))

; A quoted syntax value retains both occurrences of an undemanded argument.
(= (need-quote-twice $x) (quote (Pair $x $x)))
!(need-quote-twice (need-add-one 7))
(= (need-two-quotes $x) (Pair (quote $x) (quote $x)))
!(need-two-quotes (need-add-one 7))

; Outside quotation, both uses share the demanded computation.
(= (need-two-values $x) (Pair $x $x))
!(need-two-values (need-add-one 7))
!(quote (quote (+ 1 2)))

; Carrying an effect's source under quotation does not perform the effect.
!(bind! &need-quote-counter (new-state 0))
(= (need-quote-effect)
   (let $old (get-state &need-quote-counter)
     (let $_ (change-state! &need-quote-counter (+ $old 1)) $old)))
!(need-two-quotes (need-quote-effect))
!(get-state &need-quote-counter)
