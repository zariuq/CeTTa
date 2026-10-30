; The quote-as-written profile, an explicit opt-in bubble: substitution
; enters quotations, as HE's does.  Prime's default seals quotations instead
; (see tests/prime/routes/quote_seal.metta), and this fixture runs only under
; the bubble.
;
; A variable denotes what its binding parameter's type says, under this
; profile too.  A value parameter denotes its argument's value: the
; quotation holds the shared cell, and observing it computes the cell once.
; A `Atom` parameter holds its argument as written, and the quotation
; captures that syntax.  The HE lane answers the same programs the same way,
; with HE's `Atom` in place of `Atom`.
(= (need-quote $x) (quote $x))
!(need-quote (+ 1 2))
(: need-quote-syntax (-> Atom Proc))
(= (need-quote-syntax $x) (quote $x))
!(need-quote-syntax (+ 1 2))

; The same through an ordinary constructor.
(= (need-quote-box $x) (quote (box $x)))
!(need-quote-box (* (+ 1 2) 4))
(: need-quote-box-syntax (-> Atom Proc))
(= (need-quote-box-syntax $x) (quote (box $x)))
!(need-quote-box-syntax (* (+ 1 2) 4))

; A value-demanding body still evaluates the argument.
(= (need-add-one $x) (+ $x 1))
!(need-add-one (+ 1 2))
