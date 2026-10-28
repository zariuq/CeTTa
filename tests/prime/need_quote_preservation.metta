; Default Prime seals quotations; an explicit crossing lets a parameter into
; one.  A variable denotes what its binding parameter's type says.  A value
; parameter denotes its argument's value: the quotation holds the shared
; cell, nothing is forced when the quotation is formed, and observing or
; matching it computes the cell once.  A `(Data Atom)` parameter holds its
; argument as written, so it denotes that syntax: source capture is asked
; for by the parameter's type, never decided by evaluation timing.
; quote_profiles/need_quote_preservation.metta tests the same programs under
; the quote-as-written profile; routes/quote_seal.metta tests sealing.
(= (need-quote $x) (quote $x){$x})
!(need-quote (+ 1 2))
(: need-quote-syntax (-> (Data Atom) Atom))
(= (need-quote-syntax $x) (quote $x){$x})
!(need-quote-syntax (+ 1 2))

; The same through an ordinary constructor.
(= (need-quote-box $x) (quote (box $x)){$x})
!(need-quote-box (* (+ 1 2) 4))
(: need-quote-box-syntax (-> (Data Atom) Atom))
(= (need-quote-box-syntax $x) (quote (box $x)){$x})
!(need-quote-box-syntax (* (+ 1 2) 4))

; The cell is one value however often the quotation shows it: one choice of
; the argument, two quoted uses.
(= (need-quote-twice $x) (Pair (quote $x){$x} (quote $x){$x}))
!(need-quote-twice (superpose (1 2)))

; Forming the quotation forces nothing; observing it runs the effect once.
(= (need-quote-effect $x) (quote $x){$x})
!(need-quote-effect (let $_ (println! computed) 5))
!(let $q (need-quote-effect (let $_ (println! never-observed) 5)) formed)

; A value-demanding body evaluates the argument, as before.
(= (need-add-one $x) (+ $x 1))
!(need-add-one (+ 1 2))
