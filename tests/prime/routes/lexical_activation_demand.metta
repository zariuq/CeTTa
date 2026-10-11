; Allocating a pattern scope does not evaluate its source or continuation.
(set:profile hol)
!(bind! &theory (new-space))
!(bind! &number 7)
(= (pending $space)
   (let $unused (quote marker)
     (set:proves $space (quote missing-proof) (quote missing-proposition))))
!(pending &theory)
; The consumer that requests the value still resolves the same registry name.
!(let $n &number (+ $n 1))
!(let $unused (quote marker)
   (set:proves &theory (quote missing-proof) (quote missing-proposition)))
