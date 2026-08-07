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
