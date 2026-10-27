; Default Prime seals quotations.  The explicit crossing captures the
; argument as syntax without forcing its arithmetic expression.
; quote_profiles/need_quote_preservation.metta tests implicit capture under
; the separate quote-as-written profile; routes/quote_seal.metta tests sealing.
(= (need-quote $x) (quote $x){$x})
!(need-quote (+ 1 2))

; Syntax preservation is recursive through an ordinary constructor.
(= (need-quote-box $x) (quote (box $x)){$x})
!(need-quote-box (* (+ 1 2) 4))

; Conversely, a value-demanding body still evaluates the argument.
(= (need-add-one $x) (+ $x 1))
!(need-add-one (+ 1 2))
