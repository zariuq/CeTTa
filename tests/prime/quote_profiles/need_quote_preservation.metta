; The quote-as-written profile, an explicit opt-in bubble: substitution
; enters quotations, as HE's does, so a quoted parameter captures its
; argument as written.  Prime's default seals quotations instead (see
; tests/prime/routes/quote_seal.metta), and this fixture runs only under the
; bubble.
;
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
