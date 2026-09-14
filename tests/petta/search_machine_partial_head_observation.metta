; An open constructor identity is compatible with each same-arity head.
; Other known fields and shared variables remain independent constraints.
(= (partial-choice (a one)) first)
(= (partial-choice (b one)) second)
(= (partial-choice (a one)) first)
(= (partial-choice (a one two)) extended)
!(partial-choice ($head one))
!(partial-choice ($head wrong))
!(partial-choice ($head one two))

(= (partial-pair (a v) (b v)) separate)
!(partial-pair ($head v) ($head v))
!(partial-pair ($left v) ($right v))
