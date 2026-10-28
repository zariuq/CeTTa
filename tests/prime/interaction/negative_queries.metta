; Complete nested queries use the ordinary retained interaction engine.
!(import! &self interaction)
!(bind! &q (new-space))

; Empty conjunction and vacuous every are successful rows.
!(collapse (transform &q (absent ()) impossible))
!(collapse (transform &q (absent (every (read (msg $x)))) impossible))
!(collapse (transform &q (absent (, (every (read (msg $x))))) impossible))

; A computed source keeps its actual complete answer bag.
!(collapse (transform &q (absent (let $x 7)) impossible))
!(collapse (transform &q (absent (let $x (empty))) no-values))
!(collapse (transform &q
   (absent (, (let $x (superpose (1 2))) (read (msg $x)))) no-joined-row))
!(add-atom &q (msg 2))
!(collapse (transform &q
   (absent (, (let $x (superpose (1 2))) (read (msg $x)))) impossible))
!(collapse (transform &q
   (absent (, (let $x 1) (read (msg $x)))) no-message-one))

; The inner query's bindings and resource picks do not escape.
!(transform &q
   (, (absent (, (let $x 1) (read (msg $x)))) (let $x 9)) (local $x))
!(collapse (transform &q (absent (, (take (msg $x)) (take (msg $x)))) impossible))
!(collapse (match &q (msg $x) $x))

; Nested absence tests the existence of an inner row.
!(collapse (transform &q (absent (absent (read (ack $x)))) impossible))
!(add-atom &q (ack 2))
!(transform &q (absent (absent (read (ack $x)))) acknowledgement-exists)

; An aggregate row exists even when all contributing rows are absent,
; or when their combined takes could not form a linear commit.
!(collapse (transform &q (absent (all (read (missing $x)) $xs)) impossible))
!(collapse (transform &q
   (absent (every (, (let $x (superpose (1 2))) (take (msg 2))))) impossible))
!(collapse (match &q (msg $x) $x))
!(collapse (transform &q (absent (unique (read (msg $x)))) impossible))
!(add-atom &q (msg 2))
!(transform &q (absent (unique (read (msg $x)))) not-unique)

; Computation effects are observable even when a query returns no row.
; An unsuccessful guard does not roll them back or repeat their evaluation.
!(bind! &effects (new-space))
(= (query-tick $value)
   (let $_ (add-atom &effects tick) $value))
!(transform &q (absent (, (let $x (query-tick 1)) (read (msg $x)))) no-ticked-message)
!(collapse (get-atoms &effects))
!(collapse (transform &q (absent (let $x (query-tick 7))) impossible))
!(size-atom (collapse (get-atoms &effects)))

; A nested every remains one row within an enclosing every.
!(collapse (transform &q
   (absent (every (every (read (missing $x))))) impossible))
!(assertEqual (collapse (transform &q
   (every (every (read (msg $x)))) (seen $x))) ((seen 2) (seen 2)))

; A nonlinear inner aggregate is filtered as one row by the outer every.
!(bind! &nested-out (new-space))
!(add-atom &q gate)
!(add-atom &q marker)
!(let $_ (transform &q
   (, (take marker) (every (every (, (read (msg $x)) (take gate)))))
   (add-atom &nested-out impossible))
   (assertEqual (collapse (match &q marker marker)) ()))
!(assertEqual (collapse (match &q gate gate)) (gate))
!(assertEqual (collapse (get-atoms &nested-out)) ())
