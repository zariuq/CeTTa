; A constructor's declared signature decides which of its arguments are
; computed, in an equation's result exactly as in a query: an argument at an
; Atom-typed position is kept as written, one at an %Undefined% position is
; computed.  The prepared equation path must agree with the canonical one.
(: r:val (-> Atom Atom))
(: r:two (-> Atom %Undefined% Atom))
(= (h $n) (+ $n 1))
(= (f1) (r:val (h 1)))
(= (f2 $x) (r:val (deep $x (h 1))))
(= (f3) (r:two (h 1) (h 2)))
(: f4 (-> Atom))
(= (f4) (r:val (h 1)))
(= (f5) (let $y 1 (r:val (h $y))))
(= (f6) (plain (r:val (h 1))))
(= (f7) (plain (h 1)))
!(B0 (r:val (h 1)))
!(B1 (f1))
!(B2 (f2 7))
!(B3 (f3))
!(B4 (f4))
!(B5 (f5))
!(B6 (f6))
!(B7 (f7))
