; `Atom` is the top type and controls no evaluation.  A function whose
; declared result type is Atom returns its evaluated result, as with any
; other result type or none.  A function that returns syntax builds it as
; code: a quotation is sealed, so the argument's value enters the code by lift.
(: to-atom (-> Number Atom))
(= (to-atom $x) (+ $x 1))
(: to-number (-> Number Number))
(= (to-number $x) (+ $x 1))
(= (untyped $x) (+ $x 1))
!(to-atom 2)
!(to-number 2)
!(untyped 2)
(: quoting (-> Number Atom))
(= (quoting $x) (lift let x (lift $x) @(+ x 1)))
!(quoting 2)
; A Data parameter still holds its argument.
(: held (-> (Data Atom) Atom))
(= (held $x) $x)
!(held (+ 1 2))
